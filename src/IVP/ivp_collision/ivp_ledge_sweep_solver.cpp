// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ============================================================================
// ivp_ledge_sweep_solver.cpp
//
// The three generic solver functions behind the Csys sweep-test driver family
// (decomp-tracker Common.dll items 3596 / 3597 / 3598):
//
//   FUN_06369280  ->  IVP_Ledge_Sweep_Evaluator::RefineContactTime
//   FUN_063694a0  ->  IVP_Ledge_Sweep_Evaluator::SearchContactTime
//   FUN_06369720  ->  IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact
//
// WHAT THIS CLUSTER ACTUALLY IS (2026-09-17, lane claude:w19-csys-sweep).
// Until this pass all three were parked as "private support machinery of the
// Baraff-style Simplex/LCP contact solver, inside the excluded
// 0x06363000-0x0636e000 range". That attribution is wrong and every one of
// its consequences was wrong with it. Reading the three bodies end to end
// against Common.dll.txt (NOT Common.dll.c) shows:
//
//   * there is no permutation array, no ratio test, no pivot selection and
//     no matrix anywhere in the 586 instructions;
//   * what there IS: a distance functor invoked through a one-slot vtable, a
//     scalar threshold comparison, a step of (distance / relative speed), a
//     pair of lazily-filled 21-entry transform caches keyed by a 1/200-second
//     grid, and a regula-falsi bracket refinement.
//
// That is textbook conservative advancement / time-of-contact search. The
// functions live at LCP-adjacent addresses and belong to the sweep test.
//
// TIME REPRESENTATION. Every "time" here is a split IVP_Time {time_base,
// time_offset} pair passed as two separate floats, exactly as retail passes
// it. The solvers keep time_offset normalized into (-inf, 1.0] with an
// explicit `while (offset > 1.0f) { base += 1.0f; offset -= 1.0f; }` loop --
// the same loop shape appears three times across the three functions
// (:063695df, :06369882, :06369302) and is transcribed literally each time
// rather than replaced with fmod, because retail's version is a loop and its
// float rounding is observable in the result.
//
// .rdata CONSTANTS, read as raw bytes (a widened-float trap was checked for
// and does not apply -- only DAT_063a58b0 is a genuine 8-byte double, and it
// is all zeroes):
//   DAT_063a53d0 = 00 00 80 3f              -> 1.0f
//   DAT_063a53d4 = 00 00 00 00              -> 0.0f
//   DAT_063a545c = a6 9b 44 3b              -> 0.003f (0x3b449ba6)
//   DAT_063a5654 = 0a d7 a3 3b              -> 0.005f (0x3ba3d70a)
//   DAT_063a58a8 = 00 00 c0 3e              -> 0.375f
//   DAT_063a58ac = 00 00 48 43              -> 200.0f  (== 1.0f / 0.005f)
//   DAT_063a58b0 = 00 00 00 00 00 00 00 00  -> (double)0.0
//
// x87 BRANCH POLARITY. Every FCOM/FCOMP in this file was resolved by reading
// the MASK and the JUMP together, never the predicate alone:
//   TEST AH,0x41 + JZ   -> taken when ST0 >  mem   (C3 and C0 both clear)
//   TEST AH,0x41 + JNZ  -> taken when ST0 <= mem   (or unordered)
//   TEST AH,0x01 + JZ   -> taken when ST0 >= mem
//   TEST AH,0x01 + JNZ  -> taken when ST0 <  mem
// The one place this matters most is :063694e5 vs :06369764, where the two
// entry points test the SAME comparison with OPPOSITE jumps -- see
// SearchContactTimeFromContact's comment.
//
// x87 PRECISION (cmn-ivp3g). Values retail keeps on the x87 stack are
// `double` here and are rounded to float only where the listing has an
// FST/FSTP dword; compares that retail makes on the unrounded ST0 compare the
// double. Every test is written so the unordered (NaN) case takes the same
// side as retail's mask/jump pair (unordered sets C3, C2 and C0).
// ============================================================================

#include "ivp_ledge_sweep_solver.h"

#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "ivp_compact_ledge_solver.h" // calc_hesse_vec_object_not_normized (FUN_0635b940)
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte (IVP_Sweep_Frame_Cache::Init)

#include <cmath>

// ============================================================================
// FUN_06369280 -- IVP_Ledge_Sweep_Evaluator::RefineContactTime
// Common.dll.txt:468718-468884 (136 insns). Sole caller: SearchContactTime
// below, at :063696d9.
//
// Regula falsi (false position) on the bracket
// [(LoTimeBase,LoTimeOffset) -> LoDistance, (HiTimeBase,HiTimeOffset) ->
// HiDistance], solving Evaluate(...) == Threshold, with an Illinois-style
// pull toward the bracket midpoint applied on every 4th iteration to stop the
// classic false-position stagnation on one endpoint.
//
// Receiver proof: ECX is spilled to local_84 at :06369298 and reloaded from
// there at :063693e6 purely to reach `MOV EDX,[ECX]; CALL [EDX]` -- i.e. the
// receiver is this evaluator, and the only thing done with it is the slot-0
// virtual call. It is never used as a data pointer here.
// ============================================================================
void IVP_Ledge_Sweep_Evaluator::RefineContactTime(IVP_Time *OutTime,
                                                  float LoTimeBase, float LoTimeOffset,
                                                  float HiTimeBase, float HiTimeOffset,
                                                  float Threshold,
                                                  float LoDistance, float HiDistance,
                                                  IVP_Real_Object *ObjectA,
                                                  IVP_Real_Object *ObjectB)
{
    // local_88 at :0636929c. Counts only the iterations that did NOT converge.
    int Iteration = 0;

    // EBX / EBP. Retail keeps the low end of the bracket in these two
    // registers AND mirrored in the param_2/param_3 stack slots; the two are
    // written together at :06369432/:06369439 and never diverge, so one pair
    // of locals models both.
    float ResultBase = LoTimeBase;
    float ResultOffset = LoTimeOffset;

    for (;;)
    {
        // LAB_063692a4. ESI/local_90 and EDI/local_8c.
        //
        // :063692ab writes `local_8c = EBP` and :063692b8 writes
        // `local_90 = ESI`, but local_8c is unconditionally overwritten eight
        // instructions later by the FST at :063692f1, so only local_90's
        // initialization survives.
        float MidBase = ResultBase;

        // :063692a4-:063692ea, FPU stack simulated -- the whole expression
        // stays on the x87 stack (extended, hence double here):
        //   FLD param_4; FSUB param_2; FADD param_5; FSUB param_3  -> DeltaTime
        //   FLD param_6; FSUB param_7                              -> (T - LoD)
        //   FMULP                                                  -> DeltaTime*(T-LoD)
        //   FLD param_8; FSUB param_7                              -> (HiD - LoD)
        //   FDIVP  (ST1/ST0, then pop)
        //   FADD param_3
        // :063692f1 FST dword local_8c rounds the stored copy; :063692f5
        // FCOMP 1.0f compares the UNROUNDED value.
        double MidOffsetExt =
            ((((double)(HiTimeBase) - LoTimeBase) + HiTimeOffset) - LoTimeOffset)
                * ((double)(Threshold) - LoDistance)
                / ((double)(HiDistance) - LoDistance)
            + LoTimeOffset;
        float MidOffset = (float)(MidOffsetExt);

        // :063692f5 FCOMP 1.0f / TEST AH,0x41 / JNZ LAB_0636932f -- the
        // normalization at LAB_06369302 runs only while the offset is
        // strictly greater than 1.0f (unordered skips it). Each pass:
        // base FLD/FADD 1.0/FSTP dword; offset FLD/FSUB 1.0/FST dword, then
        // FCOMP 1.0f on the unrounded difference (:06369302-:06369329).
        while (MidOffsetExt > 1.0)
        {
            MidBase += 1.0f;
            MidOffsetExt = (double)(MidOffset) - 1.0;
            MidOffset = (float)(MidOffsetExt);
        }

        // :0636932f-:0636933b. `AND ECX,3 / CMP CL,3` -- the safeguard fires
        // on iterations 3, 7, 11, ...
        if ((Iteration & 3) == 3)
        {
            // :0636933d `CMP EAX,0x40 / JG LAB_0636945f`. Strictly greater
            // than 64, and the give-up exit reports the LOW end of the
            // bracket, not the current midpoint.
            if (Iteration > 0x40)
            {
                OutTime->time_base = ResultBase;
                OutTime->time_offset = ResultOffset;
                return;
            }

            // :06369346-:0636937e. Pulls the false-position point 0.375 of
            // the way toward the bracket midpoint, all on the x87 stack:
            //   ((Hi - Mid) + (Lo - Mid)) * 0.375f + Mid
            // FADDP adds the Lo group (ST0) into the Hi group (ST1). The
            // result is FST dword'd at :0636937e and FCOMP'd unrounded at
            // :06369382.
            MidOffsetExt =
                ((((((double)(HiTimeBase) - MidBase) + HiTimeOffset) - MidOffset)
                  + ((((double)(LoTimeBase) - MidBase) + LoTimeOffset) - MidOffset))
                 * (double)(0.375f))
                + MidOffset;
            MidOffset = (float)(MidOffsetExt);

            // :06369382, same mask/jump pair as above; loop :0636938f-:063693b6.
            while (MidOffsetExt > 1.0)
            {
                MidBase += 1.0f;
                MidOffsetExt = (double)(MidOffset) - 1.0;
                MidOffset = (float)(MidOffsetExt);
            }
        }

        // :063693bc-:063693f6. Two transform evaluations at the candidate
        // time, then the evaluator. Argument order proved by PUSH order:
        // :063693cd pushes ESI (time_base) last of the three, so it is
        // FUN_06345850's first argument; :063693f5 pushes &local_40 (the
        // ObjectA frame) last of the two, so it is Evaluate's first argument.
        IVP_Time MidTime;
        MidTime.time_base = MidBase;
        MidTime.time_offset = MidOffset;

        IVP_U_Matrix FrameA;
        IVP_U_Matrix FrameB;
        ObjectA->get_m_world_f_object_at_transform(MidTime, &FrameA);
        ObjectB->get_m_world_f_object_at_transform(MidTime, &FrameB);

        // The evaluator's result stays on the x87 stack for both tests and is
        // only FSTP'd (dword) into the moved bracket end (cmn-ivp3f 2026-10-09).
        const double MidDistanceExt = Evaluate(&FrameA, &FrameB);
        const float MidDistance = (float)(MidDistanceExt);

        // :063693f8-:0636940e: FLD ST0 / FSUB Threshold / FABS / FCOMP 0.003f
        // / TEST AH,0x1 / JNZ LAB_06369478 -- converged when strictly less
        // OR unordered (C0 is set for both), hence the negated >=.
        if (!(std::fabs(MidDistanceExt - Threshold) >= (double)(0.003f)))
        {
            OutTime->time_base = MidBase;
            OutTime->time_offset = MidOffset;
            return;
        }

        // :06369410-:06369425. The counter is incremented here, i.e. only on
        // a non-converged iteration.
        Iteration = Iteration + 1;

        // :06369414 FCOM / TEST AH,0x1 / JNZ LAB_06369445 -- strictly less
        // than Threshold (or unordered) moves the HIGH end of the bracket
        // down to the midpoint (FSTP dword into param_8 at :06369445).
        if (!(MidDistanceExt >= Threshold))
        {
            HiDistance = MidDistance;
            HiTimeBase = MidBase;
            HiTimeOffset = MidOffset;
        }
        else
        {
            ResultBase = MidBase;
            ResultOffset = MidOffset;
            LoDistance = MidDistance;
            LoTimeBase = MidBase;
            LoTimeOffset = MidOffset;
        }
    }
}

// ============================================================================
// FUN_063694a0 -- IVP_Ledge_Sweep_Evaluator::SearchContactTime
// Common.dll.txt:468899-469141 (207 insns).
//
// Conservative advancement from (CurTimeBase, CurTimeOffset) toward
// (EndTimeBase, EndTimeOffset): while the evaluator's distance is above
// Threshold, advance by up to twice (distance / relative speed), quantized
// down onto a 1/200 s grid, filling both caches' transform slots on demand;
// as soon as the distance drops to or below Threshold, hand the bracket to
// RefineContactTime.
//
// Returns 1 and writes *OutTime when a contact time at or before the end of
// the interval was found, 0 otherwise.
//
// Receiver proof: ECX is copied to EBP at :063694b0 and spilled to local_c at
// :063694b7. EBP is then reused as a scratch float-bits register from
// :063695b6 onward, and is deliberately RELOADED from local_c at :0636968c
// before the loop branches back -- precisely because `FMUL [EBP+8]` at
// :06369523 needs the evaluator again. local_c is also the source for the
// virtual call at :0636965e. So the receiver is this evaluator and both
// +0x08 and the vtable belong to it.
//
// CACHE-SLOT BOUND. Retail's only guard is `CMP ESI,0x14 / JZ` at :0636967b,
// i.e. exact equality with 20, evaluated AFTER the slot has been indexed.
// That is safe only because the advance per step can never exceed the
// remaining interval: the `Remaining + Step > 0` early-out caps the
// single-step case, and the doubled case is taken only when
// `Remaining + 2*Step <= 0`. Transcribed verbatim rather than clamped, but
// flagged here because it is the one place where a caller passing an
// interval longer than 21/200 s would walk off FrameAtSlot[] into
// FrameStorage[].
// ============================================================================
int IVP_Ledge_Sweep_Evaluator::SearchContactTime(float Threshold,
                                                 float CurTimeBase, float CurTimeOffset,
                                                 float EndTimeBase, float EndTimeOffset,
                                                 int StartSlot,
                                                 IVP_Sweep_Frame_Cache *CacheA,
                                                 IVP_Sweep_Frame_Cache *CacheB,
                                                 const float *KnownDistance,
                                                 IVP_Time *OutTime)
{
    // :063694a3-:063694d4. param_9 is an optional pointer to an
    // already-computed distance at the start of the interval; when it is null
    // the evaluator is asked for it. Slot 0 of both caches is guaranteed
    // non-null by IVP_Sweep_Frame_Cache's constructor, hence no null check.
    float Distance;
    if (KnownDistance != nullptr)
    {
        Distance = *KnownDistance;
    }
    else
    {
        Distance = (float)(Evaluate(CacheA->FrameAtSlot[0], CacheB->FrameAtSlot[0])); // 063694d4 FSTP dword
    }

    // :063694e2 TEST AH,0x41 / :063694e5 JZ LAB_06369507. The jump is taken
    // when Distance > Threshold (march); the FALL-THROUGH is Distance <=
    // Threshold (or unordered), which is an immediate hit at the interval
    // start.
    if (!(Distance > Threshold))
    {
        OutTime->time_base = CurTimeBase;
        OutTime->time_offset = CurTimeOffset;
        return 1;
    }

    // :06369507-:06369517. ESI, local_8, local_4.
    int Slot = StartSlot;
    float PrevTimeBase = CurTimeBase;
    float PrevTimeOffset = CurTimeOffset;

    for (;;)
    {
        // LAB_0636951b. `FMUL [EBP+8]` -- multiply by the reciprocal speed,
        // not divide by the speed. Stays on the x87 stack (ST1 below).
        const double Step = ((double)(Distance) - Threshold) * IMaxRelativeSpeed;

        // :06369526-:06369536. Negative while the current time is before the
        // end of the interval. :06369536 FST dword param_8 keeps a ROUNDED
        // copy for the second test; the first test (:0636953a FADD ST0,ST1)
        // uses the unrounded value still in ST0.
        const double RemainingExt =
            (((double)(PrevTimeBase) - EndTimeBase) + PrevTimeOffset) - EndTimeOffset;
        const float Remaining = (float)(RemainingExt);

        // :06369544 TEST AH,0x41 / :06369547 JZ LAB_0636969d -- the jump
        // (return 0) is taken when Remaining + Step is STRICTLY GREATER than
        // zero, i.e. one conservative step already overshoots the end of the
        // interval, so no contact can occur inside it.
        if (RemainingExt + Step > 0.0)
        {
            return 0;
        }

        // :0636954d FADD ST0,ST0 doubles the step (exact).
        const double DoubledStep = Step + Step;

        double Advance;

        // :0636954f FLD param_8 (the rounded Remaining) / FADD ST0,ST1.
        // :0636955d TEST AH,0x41 / :06369560 JNZ LAB_0636957c -- the jump
        // (take the doubled step) is taken when Remaining + 2*Step is LESS
        // THAN OR EQUAL TO zero (or unordered). The fall-through means two
        // steps would overshoot, so retail jumps straight to the end of the
        // interval (plus a 0.003 epsilon) and tests there.
        if ((double)(Remaining) + DoubledStep > 0.0)
        {
            // :06369564-:06369574, extended.
            Advance = ((((double)(EndTimeBase) - PrevTimeBase) + EndTimeOffset) - PrevTimeOffset) +
                      (double)(0.003f);
        }
        else
        {
            Advance = DoubledStep;

            // :0636957c FCOM against the 8-byte DAT_063a58b0. Read as raw
            // bytes it is a genuine (double)0.0, not a widened float -- the
            // only real double in this cluster. TEST AH,0x1 / JZ keeps the
            // value when >= 0; strictly negative OR unordered loads 0.0f.
            if (!(Advance >= 0.0))
            {
                Advance = 0.0;
            }
        }

        // LAB_06369591. Quantize onto the 1/200 s grid, at least one grid
        // step. FMUL 200.0f on the extended value, then `ftol`, which
        // truncates toward zero, matching C's int conversion.
        int StepCount = static_cast<int>(Advance * (double)(200.0f));
        if (StepCount < 1)
        {
            StepCount = 1;
        }

        Slot = Slot + StepCount;

        // :063695ae-:063695d2. FILD count / FMUL 0.005f / FADD local_4 on the
        // x87 stack, FST dword param_3, FCOMP 1.0f on the unrounded sum.
        // Normalization loop :063695df-:06369606: base FLD/FADD/FSTP dword,
        // offset FLD/FSUB/FST dword then FCOMP on the unrounded difference.
        float NewTimeBase = PrevTimeBase;
        double NewTimeOffsetExt =
            (double)(StepCount) * (double)(0.005f) + PrevTimeOffset;
        float NewTimeOffset = (float)(NewTimeOffsetExt);
        while (NewTimeOffsetExt > 1.0)
        {
            NewTimeBase += 1.0f;
            NewTimeOffsetExt = (double)(NewTimeOffset) - 1.0;
            NewTimeOffset = (float)(NewTimeOffsetExt);
        }

        IVP_Time SlotTime;
        SlotTime.time_base = NewTimeBase;
        SlotTime.time_offset = NewTimeOffset;

        // LAB_0636960c. EBX is param_7 (CacheA), so CacheA is filled first
        // here -- the opposite order from SearchContactTimeFromContact, which
        // fills param_7 first too but holds it in EDI. The stored pointer is
        // always the cache's own inline storage for that slot:
        // `LEA EAX,[ESI*0x40 + EBX + 0x68]`.
        if (CacheA->FrameAtSlot[Slot] == nullptr)
        {
            CacheA->FrameAtSlot[Slot] = &CacheA->FrameStorage[Slot];
            CacheA->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheA->FrameStorage[Slot]);
        }

        // LAB_0636962f. Retail latches CacheA's pointer into the param_8 slot
        // at :06369639 BEFORE possibly creating CacheB's, then reloads
        // CacheB's fresh at :0636965a.
        IVP_U_Matrix *FrameA = CacheA->FrameAtSlot[Slot];

        if (CacheB->FrameAtSlot[Slot] == nullptr)
        {
            CacheB->FrameAtSlot[Slot] = &CacheB->FrameStorage[Slot];
            CacheB->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheB->FrameStorage[Slot]);
        }

        // :0636966c FST dword, then :06369670 FCOMP on the unrounded value.
        const double NewDistanceExt = Evaluate(FrameA, CacheB->FrameAtSlot[Slot]);
        const float NewDistance = (float)(NewDistanceExt);

        // :06369676 TEST AH,0x41 / :06369679 JNZ LAB_063696ab -- taken when
        // NewDistance <= Threshold (or unordered), i.e. the bracket
        // [Prev, New] straddles the contact.
        if (!(NewDistanceExt > Threshold))
        {
            // :063696ab-:063696d9. Retail passes the ADDRESS of its own
            // param_2 slot, so RefineContactTime writes back over
            // (CurTimeBase, CurTimeOffset); the result is then range-checked
            // and reported.
            IVP_Time Refined;
            RefineContactTime(&Refined, PrevTimeBase, PrevTimeOffset,
                              NewTimeBase, NewTimeOffset, Threshold,
                              Distance, NewDistance,
                              CacheA->Object, CacheB->Object);
            CurTimeBase = Refined.time_base;
            CurTimeOffset = Refined.time_offset;

            // :063696de-:063696ee, extended. :063696f6 TEST AH,0x41 /
            // :063696f9 JZ LAB_0636969f -- return 0 when the refined time
            // landed strictly past the end.
            if ((((double)(CurTimeBase) - EndTimeBase) + CurTimeOffset) - EndTimeOffset > 0.0)
            {
                return 0;
            }

            OutTime->time_base = CurTimeBase;
            OutTime->time_offset = CurTimeOffset;
            return 1;
        }

        // :0636967b. Exact equality with 20 -- see the CACHE-SLOT BOUND note
        // in this function's header comment.
        if (Slot == 0x14)
        {
            return 0;
        }

        // :06369680-:06369698.
        PrevTimeBase = NewTimeBase;
        PrevTimeOffset = NewTimeOffset;
        Distance = NewDistance;
    }
}

// ============================================================================
// FUN_06369720 -- IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact
// Common.dll.txt:469148-469420 (243 insns).
//
// The entry point the drivers actually call (8 external call sites, all in
// 0x0635dc22-0x0635faff). It handles the case where the two bodies are
// ALREADY within Threshold at the start of the interval, which
// SearchContactTime cannot: that function would report an immediate hit at
// t0 and never move.
//
// The polarity of the entry test is the single most important detail in this
// file and is the mirror image of SearchContactTime's:
//   SearchContactTime  :063694e5  TEST AH,0x41 + JZ   -> march when d >  T
//   this function      :06369764  TEST AH,0x41 + JNZ  -> loop  when d <= T
// So when the bodies start APART this function just delegates to
// SearchContactTime, and when they start TOUCHING it runs its own loop.
//
// That loop walks forward while the separation keeps growing but stays inside
// Threshold, and stops the moment the separation stops growing
// (NewDistance <= the distance measured at t0) -- reporting the PREVIOUS grid
// time, i.e. the last sample before the bodies began closing again. If the
// separation instead leaves the Threshold band, the remainder of the interval
// is handed to SearchContactTime, starting from the slot already reached and
// re-using the distance already measured.
//
// Note the two differences from SearchContactTime's stepping, both verified
// instruction by instruction rather than assumed from the family resemblance:
//   * the step is measured against a SECOND, smaller threshold
//     (InnerThreshold, param_2, `FSUB [ESP+0x24]` at :063697e2) instead of
//     against Threshold;
//   * the step is NOT doubled -- there is no `FADD ST0,ST0` here, and the
//     overshoot test at :06369804 has the opposite jump sense to
//     :0636955d's, so overshoot means "jump to the end of the interval"
//     rather than "return 0".
//
// It also divides by MaxRelativeSpeed (+0x04) directly at :063697a8 instead
// of multiplying by IMaxRelativeSpeed (+0x08). The drivers set +0x08 to
// exactly 1.0f / +0x04 (Common.dll.txt:0635db9d-0635dbba), so the two agree.
//
// Receiver proof: ECX is copied to ESI at :06369738 and spilled to local_c at
// :0636973d. ESI is then used BOTH as the receiver of the slot-0 virtual call
// at :06369743 AND as the base of the `FDIV [ESI+4]` at :063697a8 -- one
// register, one object, loaded from ECX, so +0x04 is a field of this
// evaluator and not of either cache.
// ============================================================================
int IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact(float Threshold, float InnerThreshold,
                                                            float CurTimeBase, float CurTimeOffset,
                                                            float EndTimeBase, float EndTimeOffset,
                                                            IVP_Sweep_Frame_Cache *CacheA,
                                                            IVP_Sweep_Frame_Cache *CacheB,
                                                            const float *KnownDistance,
                                                            IVP_Time *OutTime)
{
    // :06369723-:06369753.
    float Distance;
    if (KnownDistance != nullptr)
    {
        Distance = *KnownDistance;
    }
    else
    {
        Distance = (float)(Evaluate(CacheA->FrameAtSlot[0], CacheB->FrameAtSlot[0])); // 0636974b FSTP dword
    }

    // :06369761 TEST AH,0x41 / :06369764 JNZ LAB_0636979e. The FALL-THROUGH
    // (Distance strictly greater than Threshold) is the delegation.
    if (Distance > Threshold)
    {
        // :06369766-:0636978f, argument order recovered from PUSH order:
        // param_6 (the start slot) is the literal 0 pushed at :0636977e, and
        // param_9 is `LEA EDX,[ESP+0x3c]`, the address of the local holding
        // the distance just computed.
        return SearchContactTime(Threshold, CurTimeBase, CurTimeOffset,
                                 EndTimeBase, EndTimeOffset, 0,
                                 CacheA, CacheB, &Distance, OutTime);
    }

    // LAB_0636979e. FLD 1.0 / FDIV [ESI+4] / FSTP dword param_9 -- the
    // reciprocal is rounded to float (the param_9 slot) before use.
    float InvSpeed = 1.0f / MaxRelativeSpeed;
    int Slot = 0;
    float PrevTimeBase = CurTimeBase;
    float PrevTimeOffset = CurTimeOffset;

    // :063697c1-:063697d1, extended. :063697d9 TEST AH,0x1 / :063697dc JZ
    // LAB_063699e3 -- return 0 when the difference is >= 0; strictly
    // negative OR unordered (C0 set) enters the loop.
    if ((((double)(CurTimeBase) - EndTimeBase) + CurTimeOffset) - EndTimeOffset >= 0.0)
    {
        return 0;
    }

    // :063697bd FLD dword; from the second iteration on this is the
    // evaluator's unrounded result left on the x87 stack (:0636994e loops back
    // with it in ST0), hence double.
    double CurDistance = Distance;

    for (;;)
    {
        // LAB_063697e2. Measured against InnerThreshold, and divided by the
        // speed via the reciprocal computed once above.
        // :063697e2-:063697fa: Step, Remaining and their sum all stay on the
        // x87 stack (cmn-ivp3f 2026-10-09: double, was float).
        const double Step = (CurDistance - InnerThreshold) * InvSpeed;
        const double Remaining = (((double)(PrevTimeBase) - EndTimeBase) + PrevTimeOffset) - EndTimeOffset;

        double Advance;

        // :06369804 TEST AH,0x41 / :06369807 JNZ LAB_06369823 -- the jump
        // (keep the step) is taken when Remaining + Step <= 0. The
        // fall-through, an overshoot, jumps to the end of the interval.
        if (Remaining + Step > 0.0)
        {
            Advance = ((((double)(EndTimeBase) - PrevTimeBase) + EndTimeOffset) - PrevTimeOffset) +
                      (double)(0.003f);
        }
        else
        {
            Advance = Step;

            // :06369823 FCOM against the 4-byte DAT_063a53d4 (0.0f) -- note
            // this is a FLOAT compare where SearchContactTime's equivalent at
            // :0636957c uses the 8-byte DAT_063a58b0. Same value, different
            // constant; not a transcription slip. TEST AH,0x1 / JZ keeps the
            // value when >= 0; strictly negative OR unordered loads 0.0f.
            if (!(Advance >= 0.0))
            {
                Advance = 0.0;
            }
        }

        // LAB_06369838, same quantization as SearchContactTime (FMUL 200.0f
        // on the extended value, then ftol).
        int StepCount = static_cast<int>(Advance * (double)(200.0f));
        if (StepCount < 1)
        {
            StepCount = 1;
        }

        // :06369857-:063698a3. Unlike SearchContactTime, BOTH halves stay on
        // the x87 stack across the whole normalization loop: FLD local_8
        // (base) / FILD count / FMUL 0.005f / FADD local_4 (offset); then
        // FXCH / FADD 1.0 / FXCH / FSUB 1.0 / FCOM 1.0 per pass, and the two
        // FSTP dword only after the loop (:0636989f/:063698a3). When the loop
        // does not run, :0636985b FST dword param_3 (base) and :06369873 FST
        // dword param_4 (offset) give the same rounding.
        double WorkTimeBaseExt = PrevTimeBase;
        double WorkTimeOffsetExt =
            (double)(StepCount) * (double)(0.005f) + PrevTimeOffset;
        while (WorkTimeOffsetExt > 1.0)
        {
            WorkTimeBaseExt += 1.0;
            WorkTimeOffsetExt -= 1.0;
        }
        const float WorkTimeBase = (float)(WorkTimeBaseExt);
        const float WorkTimeOffset = (float)(WorkTimeOffsetExt);

        // LAB_063698a7. `ADD ESI,ECX` -- the slot advances by the same grid
        // count. There is no `Slot == 20` guard in this function at all; the
        // bound rests entirely on Advance never exceeding the remaining
        // interval (both branches above are capped by Remaining).
        Slot = Slot + StepCount;

        IVP_Time SlotTime;
        SlotTime.time_base = WorkTimeBase;
        SlotTime.time_offset = WorkTimeOffset;

        // EDI is param_7 (CacheA) here and is filled first, matching
        // SearchContactTime's param_7-first order even though the register
        // assignment is swapped between the two functions.
        if (CacheA->FrameAtSlot[Slot] == nullptr)
        {
            CacheA->FrameAtSlot[Slot] = &CacheA->FrameStorage[Slot];
            CacheA->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheA->FrameStorage[Slot]);
        }

        IVP_U_Matrix *FrameA = CacheA->FrameAtSlot[Slot];

        if (CacheB->FrameAtSlot[Slot] == nullptr)
        {
            CacheB->FrameAtSlot[Slot] = &CacheB->FrameStorage[Slot];
            CacheB->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheB->FrameStorage[Slot]);
        }

        const double NewDistanceExt = Evaluate(FrameA, CacheB->FrameAtSlot[Slot]); // :0636990b, kept in ST0

        // :0636990d FCOM (not FCOMP -- NewDistance stays live on the FPU
        // stack for the second test and for the next iteration) / TEST
        // AH,0x41 / :06369916 JZ LAB_0636996b, taken when NewDistance is
        // strictly greater than Threshold: the bodies have separated past the
        // band, so the rest of the interval is an ordinary approach search.
        if (NewDistanceExt > Threshold)
        {
            // :0636996b-:06369977 extended; :0636997b TEST AH,0x41 /
            // :06369986 JZ LAB_063699e3 -- return 0 when strictly past the end.
            if ((((double)(WorkTimeBase) - EndTimeBase) + WorkTimeOffset) - EndTimeOffset > 0.0)
            {
                return 0;
            }

            // :0636998c FSTP dword -- the handed-over distance is the float.
            float NewDistance = (float)(NewDistanceExt);

            // :06369988-:063699b2. Note param_6 is ESI, the slot already
            // reached, so SearchContactTime continues filling the SAME cache
            // slots rather than restarting at 0; and param_9 is the address
            // of the distance just measured.
            return SearchContactTime(Threshold, WorkTimeBase, WorkTimeOffset,
                                     EndTimeBase, EndTimeOffset, Slot,
                                     CacheA, CacheB, &NewDistance, OutTime);
        }

        // :0636991e TEST AH,0x41 / :06369921 JNZ LAB_063699c1, taken when
        // NewDistance <= the distance measured at the interval start. Retail
        // reports local_8/local_4 -- the PREVIOUS grid time, not the one just
        // stepped to (:063699c7/:063699cb).
        if (!(NewDistanceExt > Distance)) // <= or unordered
        {
            OutTime->time_base = PrevTimeBase;
            OutTime->time_offset = PrevTimeOffset;
            return 1;
        }

        // :06369927-:0636994e. The locals advance first, then the
        // end-of-interval test decides whether to loop.
        PrevTimeBase = WorkTimeBase;
        PrevTimeOffset = WorkTimeOffset;

        // :06369927-:06369943 extended; TEST AH,0x1 / JNZ LAB_063697e2 --
        // loop while strictly before the end OR unordered; >= 0 returns 0.
        if ((((double)(WorkTimeBase) - EndTimeBase) + WorkTimeOffset) - EndTimeOffset >= 0.0)
        {
            return 0;
        }

        CurDistance = NewDistanceExt;
    }
}

// ============================================================================
// FUN_0635de90 -- IVP_Sweep_Frame_Cache::Init
// Common.dll.txt:449711-449757, bytes 0x0635de90-0x0635def0 (36 insns,
// decomp-tracker item 3370). Read first byte to last this pass.
//
// Sixteen call sites, all of them sweep drivers, all of them paired: every
// driver seeds two of these, one per object under test (:0635daa4/:0635dad1,
// :0635df34/:0635df69, :0635e20f/:0635e258, :0635e7cf/:0635e80a,
// :0635ecc1/:0635eceb, :0635f054/:0635f07e, :0635f1b4/:0635f1e1,
// :0635f8f1/:0635f91e).
//
// WHAT THE BRANCH IS. `EAX = Object->flags; EAX <<= 0x18; CMP EAX,0x8000000;
// JL` -- a SIGNED compare after shifting the low byte into the sign position,
// which is this port's already-documented movement-state idiom: it is
// `(int8_t)(flags & 0xFF) >= 8`. See ivp_real_object.cpp's own comment on the
// same test, and ivp_core.cpp's copy of it. The two sides are NOT symmetric
// and the asymmetry is the point:
//
//   >= 8 (non-moving): every one of the 21 slots is pointed at the SAME
//        prepared frame, Source->Frame. A static object has one transform for
//        all 21 time samples, so the solvers' lazy-fill path can never fire
//        for it and FrameStorage stays untouched.
//
//   <  8 (moving): slots 20..1 are nulled and only slot 0 is seeded. The
//        solvers fill the rest on demand into FrameStorage.
//
// Either way slot 0 ends up valid, which is exactly why SearchContactTime and
// SearchContactTimeFromContact read FrameAtSlot[0] with no null check
// (:0636961d, :063698be).
//
// NOT DONE HERE, deliberately: retail precedes each of the sixteen calls with
// FUN_06273bc0(FrameStorage, 0x40, 0x15, Csys::Csys), the MSVC
// vector-constructor-iterator. Csys::Csys is `MOV EAX,ECX; RET` in full
// (Common.dll.txt 0x06261260-0x06261262), so those 21 invocations are a
// runtime no-op and there is nothing to transcribe. The 0x40 element size is
// sizeof(IVP_U_Matrix), not sizeof(this project's own Csys) -- see the
// header's note on the retail name collision.
// ============================================================================
void IVP_Sweep_Frame_Cache::Init(FIVP_Sweep_Cache_Source *Source)
{
    // :0635de95-:0635dea2. Object first, then Core through it -- retail
    // re-reads [ECX] at :0635dea0 rather than reusing EAX, and reads flags
    // through that re-read pointer, so the two are provably the same object.
    this->Object = Source->Object;
    this->Core = this->Object->core;

    // :0635dea5-:0635deb0 `SHL EAX,0x18; CMP EAX,0x8000000; JL`: the signed
    // low byte of flags against 8. Written as IVP_SignedLowByte, not as the
    // shift: MSVC folded `(int32_t)(flags << 24) < 0x08000000` into a compare
    // of the whole dword, so a moving ship (flags 0x101) took the static
    // branch, its 20 future frames were never extrapolated and the sweep never
    // found the contact (ivp_signed_byte.h).
    const int32_t MovementByte = IVP_SignedLowByte(this->Object->flags);

    IVP_U_Matrix *const PreparedFrame = &Source->Frame;

    if (MovementByte < 8)
    {
        // :0635ded0-:0635dee7. Twenty stores of zero, walking DOWN from
        // FrameAtSlot[20] to FrameAtSlot[1], then slot 0 set explicitly.
        for (int SlotIndex = 20; SlotIndex >= 1; --SlotIndex)
        {
            this->FrameAtSlot[SlotIndex] = 0;
        }
        this->FrameAtSlot[0] = PreparedFrame;
    }
    else
    {
        // :0635deb2-:0635dec4. Twenty-one stores of the same pointer.
        for (int SlotIndex = 20; SlotIndex >= 0; --SlotIndex)
        {
            this->FrameAtSlot[SlotIndex] = PreparedFrame;
        }
    }

    // :0635dec6/:0635dec9 and :0635deea/:0635deec -- both paths end with the
    // same store, and retail really does emit it twice.
    this->Field_0x10 = Source->Field_0x00;
}

// ============================================================================
// THE DRIVER LAYER  (2026-09-17, lane claude:w22-csys-drivers)
// ============================================================================
//
// Retail's global mindist tolerance block (.data:0658aa10), the one
// G_IVP_MindistSettings object (ivp_mindist_settings.h) built at DLL load.
// The two globals item 3371 reads are:
//   DAT_0658aa10 = +0x000             -- the tolerance base
//   DAT_0658aa18 = +0x008             -- the ramp's base, which is why every
//                                        retail user spells it
//                                        `FLD [idx*4 + DAT_0658aa18]`
// 2026-10-08 (claude-code:cmn-phy2): this file's second, lazily-built copy of
// the block is gone; the accessors keep their names and read the global.
#include "../ivp_physics/ivp_mindist_settings.h"

namespace
{
    float SweepToleranceRamp(uint32_t Index) { return IVP_MindistSettingsRamp(Index); }
    float SweepToleranceBase() { return G_IVP_MindistSettings.real_coll_dist; }            // DAT_0658aa10
    float SweepToleranceBias() { return G_IVP_MindistSettings.mindist_change_force_dist; } // DAT_0658ab34

    // FUN_0635b940 (the unnormalised face normal of an edge's triangle, raw
    // ledge pointer) has one port: IVP_Compact_Ledge_Solver::
    // calc_hesse_vec_object_not_normized (ivp_mindist_solve.cpp). The
    // float-precision duplicate that lived here (SweepFaceNormal) is gone
    // (cmn-ivp3e 2026-10-08); the three drivers call the solver directly.

    // ------------------------------------------------------------------------
    // The concrete evaluator item 3371 installs: .rdata:PTR_LAB_0635d5b0,
    // stored into the stack local's slot 0 at :0635df83. Its one virtual slot
    // is Evaluate_0635d5b0, already ported in the header.
    //
    // Field offsets are the driver's own writes, not a guess at a shared base
    // class (the header's note on item 3367 is why no shared base exists):
    //   +0x04 / +0x08  MaxRelativeSpeed, IMaxRelativeSpeed  (:0635dfdb/:0635dff7)
    //   +0x14 PointA   zeroed by three explicit `MOV ...,0x0` stores at
    //                  :0635df8b / :0635df93 / :0635df9b -- x, y and z. The
    //                  4th float at +0x20 is NEVER written, and Evaluate
    //                  never reads it.
    //   +0x24 Normal   filled by SweepFaceNormal then normalized in place
    //                  (:0635dfc7 / :0635dfd3).
    //   +0x34 Anchor   the feature edge's own start vertex (:0635dfbd stores
    //                  x, :0635dfc3 stores y, :0635dfb5 stores z).
    // sizeof pins the layout: 4 (vfptr) + 4*4 (base fields) + 3*0x10 = 0x44.
    // ------------------------------------------------------------------------
    class IVP_Sweep_PointPlane_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointA;   // +0x14
        IVP_U_Point Normal;   // +0x24
        IVP_U_Point Anchor;   // +0x34

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d5b0(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_PointPlane_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");

    // ------------------------------------------------------------------------
    // The second evaluator item 3369 installs: .rdata:PTR_LAB_0635da20 stored
    // at :0635dc61, whose one slot is Evaluate_0635da20 (already ported in the
    // header). A DIFFERENT concrete class from the one above -- two points,
    // not three, and the base fields are filled from a different speed bound.
    //   +0x04 / +0x08  :0635dd05 / :0635dd2b
    //   +0x14 Direction  the unit edge direction, written component-wise at
    //                    :0635ddf6 / :0635de20 / :0635de31
    //   +0x24 Normal     a 4-dword copy of the first evaluator's own normal,
    //                    :0635dc32-:0635dc5d
    // sizeof: 4 + 4*4 + 2*0x10 = 0x34.
    // ------------------------------------------------------------------------
    class IVP_Sweep_EdgeNormal_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point Direction;   // +0x14
        IVP_U_Point Normal;      // +0x24

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635da20(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeNormal_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // ------------------------------------------------------------------------
    // The three concrete evaluators item 3374 (FUN_0635e1d0) installs. Their
    // field offsets are not inferred from the evaluator bodies -- they are the
    // DRIVER'S OWN stores, and the driver's stack frame is what pins them.
    //
    // Frame arithmetic, once, because all three depend on it. The prologue is
    // `SUB ESP,0xc6c` at :0635e1d0 followed by four pushes (:0635e1d6/d7/d8 and
    // :0635e1e3), so the settled frame pointer is ESP_entry-0xc7c and that is
    // the 0xc7c in every one of Ghidra's `[ESP + local_XXX + 0xc7c]`
    // spellings -- i.e. local_XXX lives at frame+(0xc7c-XXX). That gives:
    //
    //   frame+0x6c .. +0xc4   evaluator 1 (0x58 bytes), vfptr PTR_LAB_0635e080
    //   frame+0xc4 .. +0xf8   evaluator 3 (0x34 bytes), vfptr PTR_LAB_0635d6d0
    //   frame+0xf8 .. +0x12c  evaluator 2 (0x34 bytes), vfptr PTR_LAB_0635e160
    //   frame+0x12c.. +0x6d4  CacheB      (0x5a8 bytes)
    //   frame+0x6d4.. +0xc7c  CacheA      (0x5a8 bytes)
    //
    // Those five spans TILE the frame exactly, end to end, with no gap and no
    // overlap. That is the check that the sizes below are right: if evaluator 1
    // were 0x44 bytes like IVP_Sweep_PointPlane_Evaluator above, or if
    // evaluator 3's points started anywhere but +0x14, the tiling would break.
    // ------------------------------------------------------------------------

    // Evaluator 1, frame+0x6c. vfptr stored at :0635e263; its slot is
    // Evaluate_0635e080 (item 3372, the skew-line edge-edge separation).
    //   +0x04 / +0x08  :0635e3b2 / :0635e3d5
    //   +0x14 PositionA  frame+0x80, written :0635e36e/:0635e37c/:0635e375
    //   +0x24 DirectionA frame+0x90, :0635e300/:0635e30d/:0635e31f
    //   +0x34 PositionB  frame+0xa0, :0635e39a/:0635e3a3/:0635e3ce
    //   +0x44 DirectionB frame+0xb0, :0635e351/:0635e358/:0635e343
    //   +0x54 Scale      frame+0xc0, the +-1.0f set at :0635e443/:0635e458
    // Evaluate_0635e080 reads exactly 0x14/0x24/0x34/0x44 and the float at
    // 0x54, so the driver's writes and the evaluator's reads agree field for
    // field. sizeof: 0x14 base + 4*0x10 + 4 = 0x58.
    class IVP_Sweep_EdgeEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PositionA;    // +0x14
        IVP_U_Point DirectionA;   // +0x24
        IVP_U_Point PositionB;    // +0x34
        IVP_U_Point DirectionB;   // +0x44
        float Scale;              // +0x54

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635e080(this, FrameA, FrameB);
        }
    };
    // 0x58 is the only size consistent with Scale landing on +0x54, given the
    // 0x14-byte base and IVP_U_Point's 0x10 stride -- offsetof is not used here
    // because these evaluators are polymorphic and offsetof on a non-standard-
    // layout type is only conditionally supported.
    static_assert(sizeof(IVP_Sweep_EdgeEdge_Evaluator) == 0x58,
                  "evaluator must be retail's 0x58-byte frame local");

    // Evaluator 2, frame+0xf8. vfptr stored at :0635e576; its slot is
    // Evaluate_0635e160 (item 3373), the parallel-edge degeneracy guard --
    // precisely the squared cross-product length whose reciprocal square root
    // evaluator 1 divides by.
    //   +0x04 / +0x08  :0635e553 / :0635e581
    //   +0x14 DirectionA frame+0x10c, copied from evaluator 1's +0x24
    //                    (:0635e4d2/:0635e4e3/:0635e4f2)
    //   +0x24 DirectionB frame+0x11c, copied from evaluator 1's +0x44
    //                    (:0635e502/:0635e517/:0635e526)
    // A DIFFERENT retail class from IVP_Sweep_EdgeNormal_Evaluator above
    // despite the identical 0x34 size -- different vtable, different meaning.
    class IVP_Sweep_EdgeParallel_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point DirectionA;   // +0x14
        IVP_U_Point DirectionB;   // +0x24

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635e160(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeParallel_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // Evaluator 3, frame+0xc4. vfptr stored at :0635e67e; its slot is
    // Evaluate_0635d6d0 (item 3363), `dot(FrameA->vmult(P14),
    // FrameB->vmult(P24))`.
    //   +0x04 / +0x08  :0635e5c1 (through frame+0xc8) / :0635e689
    //   +0x14 Direction  frame+0xd8, rewritten every loop iteration at
    //                    :0635e6c0/:0635e6c7/:0635e6f9
    //   +0x24 Normal     frame+0xe8, filled by SweepFaceNormal at :0635e705 and
    //                    normalized at :0635e714
    // Again a distinct retail class from the two 0x34-byte ones above; this one
    // is the only member of the three whose slot is the plain two-direction dot
    // product.
    class IVP_Sweep_EdgeFace_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point Direction;   // +0x14
        IVP_U_Point Normal;      // +0x24

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d6d0(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeFace_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // ------------------------------------------------------------------------
    // The two further concrete evaluators item 3378 (FUN_0635f180) installs.
    // (Its middle one is IVP_Sweep_PointPlane_Evaluator above, reused
    // unchanged -- same PTR_LAB_0635d5b0 vfptr, same 0x14/0x24/0x34 points.)
    //
    // ITEM 3378 OVERLAYS ALL THREE ON ONE STACK SLOT. Retail builds a single
    // object at frame+0x34 and REBINDS its vfptr twice as the function moves
    // between passes -- PTR_LAB_0635d950 at :0635f26b, PTR_LAB_0635d5b0 at
    // :0635f471, PTR_LAB_0635d830 at :0635f6ea -- reinterpreting the same
    // bytes each time. C++ cannot rebind a vptr, so the port uses three
    // separate locals. That is safe, and the check is not a hand-wave: every
    // field each successor evaluator reads is freshly written before its first
    // use. Pass 2 rewrites +0x04/+0x08 (:0635f44b/:0635f48c) and all three
    // components of +0x14 (:0635f492/:0635f499/:0635f4a0) -- note +0x14 was
    // pass 1's scalar bias and +0x18/+0x1c were pass 1's first point, so the
    // slots really are recycled, not inherited. Pass 3 likewise rewrites
    // +0x04/+0x08 (:0635f787/:0635f78b), +0x14 (:0635f71c-:0635f72a), +0x34
    // (:0635f736-:0635f742) and +0x44 (:0635f762-:0635f76d), and fills +0x24
    // inside its own loop. Nothing survives a rebind.
    // ------------------------------------------------------------------------

    // Pass 1's evaluator, frame+0x34, vfptr PTR_LAB_0635d950 (item 3367).
    // THE ODD ONE OUT of the nine: +0x14 is a float bias and the points start
    // at +0x18, not +0x14. The driver's own stores confirm it from the other
    // side -- :0635f32d writes a SCALAR to frame+0x48 (evaluator +0x14) while
    // :0635f278/:0635f285/:0635f28f write a POINT to frame+0x4c..0x54
    // (evaluator +0x18..+0x20).
    //   +0x14 Bias       (Ramp + CombinedRadius) * 0.5f
    //   +0x18 PointA     side A's feature vertex, A-local
    //   +0x28 PointB     side B's feature vertex, B-local
    //   +0x38 DirectionB side B's unit edge direction, B-local
    //   +0x48 MeasureB   the moment direction, B-local
    // sizeof: 0x14 base + 4 (bias) + 4*0x10 = 0x58.
    class IVP_Sweep_VertexEdgeMoment_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        float Bias;                // +0x14
        IVP_U_Point PointA;        // +0x18
        IVP_U_Point PointB;        // +0x28
        IVP_U_Point DirectionB;    // +0x38
        IVP_U_Point MeasureB;      // +0x48

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d950(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_VertexEdgeMoment_Evaluator) == 0x58,
                  "evaluator must be retail's 0x58-byte frame local");

    // Pass 3's evaluator, frame+0x34, vfptr PTR_LAB_0635d830 (item 3366).
    //   +0x14 PositionA  side A's feature vertex, A-local
    //   +0x24 DirectionA the current fan edge's unit direction, A-local
    //   +0x34 PositionB  side B's feature vertex, B-local
    //   +0x44 DirectionB side B's unit edge direction, B-local
    // sizeof: 0x14 base + 4*0x10 = 0x54. A DIFFERENT class from
    // IVP_Sweep_EdgeEdge_Evaluator above despite the identical point offsets:
    // different vtable, and no +0x54 scale field.
    class IVP_Sweep_EdgeFanEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PositionA;     // +0x14
        IVP_U_Point DirectionA;    // +0x24
        IVP_U_Point PositionB;     // +0x34
        IVP_U_Point DirectionB;    // +0x44

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d830(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeFanEdge_Evaluator) == 0x54,
                  "evaluator must be retail's 0x54-byte frame local");

    // ------------------------------------------------------------------------
    // FUN_06347e60, Common.dll.txt:411949-411964 (bytes 0x06347e60-0x06347e80,
    // 16 insns), read in full for this pass rather than taken from the
    // header's cross-reference. thiscall, no stack arguments, plain `RET`,
    // result in ST0. The x87 stack was simulated: it loads z, y, x, then
    // accumulates (x*x + y*y) + z*z and takes FSQRT. The association order is
    // retail's and is reproduced, because float addition is not associative.
    // Products, sums and the FSQRT all stay on the x87 stack and the result is
    // returned unrounded in ST0 (cmn-ivp3g: double, was float).
    // ------------------------------------------------------------------------
    double SweepPointLength(const IVP_U_Float_Point *Point)
    {
        return std::sqrt(((double)(Point->X) * Point->X
                          + (double)(Point->Y) * Point->Y)
                         + (double)(Point->Z) * Point->Z);
    }

    // ------------------------------------------------------------------------
    // The single evaluator item 3377 (FUN_0635f020) installs: PTR_LAB_0635d720
    // at :0635f09c, whose one slot is Evaluate_0635d720 (item 3364).
    //
    // Frame arithmetic for that driver: `SUB ESP,0xb94` plus four pushes puts
    // the frame at ESP_entry-0xba4, which is Ghidra's constant. The evaluator
    // is at frame+0x10, CacheB at frame+0x54 and CacheA at frame+0x5fc, and
    // those three spans plus the two 0x5a8-byte caches tile 0x10..0xba4 with no
    // gap -- the same end-to-end check used on item 3374.
    //   +0x04 / +0x08  :0635f0f4 / :0635f10f
    //   +0x14 PointA   frame+0x24, three explicit zero stores
    //                  (:0635f0ab/:0635f0b6/:0635f0be)
    //   +0x24 PointB   frame+0x34, three more (:0635f0c6/:0635f0d5/:0635f0e0)
    //   +0x34 Axis     frame+0x44, the normalized centre-to-centre direction
    //                  (:0635f0a4/:0635f0ce/:0635f0e8, then normize)
    // sizeof: 0x14 + 3*0x10 = 0x44. Same SIZE and same point offsets as
    // IVP_Sweep_PointPlane_Evaluator, but a different vtable and a different
    // meaning, so a separate class -- Evaluate_0635d720 reads its +0x34 as a
    // raw, UNTRANSFORMED direction, where Evaluate_0635d5b0 reads its +0x34 as
    // a point it pushes through FrameB.
    // ------------------------------------------------------------------------
    class IVP_Sweep_CenterDistance_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointA;   // +0x14
        IVP_U_Point PointB;   // +0x24
        IVP_U_Point Axis;     // +0x34

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d720(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_CenterDistance_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");

    // ------------------------------------------------------------------------
    // The fan-walk evaluator items 3375 and 3376 both install for their second
    // pass: PTR_LAB_0635d7b0 (:0635eac4 and :0635eebd), whose one slot is
    // Evaluate_0635d7b0 (item 3365, already ported in the header).
    //
    // It is a DIFFERENT retail class from IVP_Sweep_PointPlane_Evaluator and
    // IVP_Sweep_CenterDistance_Evaluator despite sharing their 0x44 size and
    // their 0x14/0x24/0x34 point offsets -- a different vtable, and its +0x34 is
    // pushed through FrameB with vmult (a direction) where PointPlane's +0x34
    // goes through vmult4 (a position) and CenterDistance's is not transformed
    // at all.
    //
    //   +0x14 PointOther  the OTHER side's feature vertex, in that side's local
    //                     frame, transformed by FrameA
    //   +0x24 PointSelf   this side's own feature vertex, transformed by FrameB
    //   +0x34 Direction   the current fan edge's unit direction, this side's
    //                     local frame, rotated by FrameB
    // Evaluate returns dot(PointSelf_world - PointOther_world, Direction_world).
    //
    // Offsets are the DRIVERS' own stores, and item 3375's frame tiling pins
    // them: its evaluator sits at frame+0x24 and its two 0x5a8-byte caches at
    // frame+0xac and frame+0x654, leaving frame+0x68..0xac for exactly the
    // seventeen dwords Ghidra names local_b94 through local_b54 -- 0x44 bytes,
    // end to end, no gap. An evaluator of any other size breaks that.
    // ------------------------------------------------------------------------
    class IVP_Sweep_VertexFanEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointOther;   // +0x14
        IVP_U_Point PointSelf;    // +0x24
        IVP_U_Point Direction;    // +0x34

        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            return IVP_LedgeSweepEvaluatorsDeferred::Evaluate_0635d7b0(this, FrameA, FrameB);
        }
    };
    static_assert(sizeof(IVP_Sweep_VertexFanEdge_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");
}

// ============================================================================
// FUN_0635df00 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeFaceContact
// Common.dll.txt:449772-449907. Bytes 0x0635df00 through the `RET 0xc` at
// 0x0635e072 inclusive -- read first instruction to last for this pass, all
// 109 of them, with the stack frame tracked through every PUSH so that each
// `[ESP + local_XXX + 0xbac]` spelling could be resolved to a real field.
// (Prologue `SUB ESP,0xb9c` plus four register pushes puts ESP at
// frame-0xbac, which is the constant in all of Ghidra's spellings; each of
// the six PUSHes before the final CALL shifts it again, and the evaluator's
// own field offsets only line up if that shifting is tracked -- e.g.
// :0635dfbd's `FSTP [ESP+0x50]` and :0635dfc3's `FSTP [ESP+0x5c]` are two
// DIFFERENT fields of the same point despite the 0xc gap, because a PUSH
// happened in between.)
//
// Decomp-tracker item 3371, the smallest of the four drivers. Its single
// caller is LAB_0635fd10's ShapeTypeB == 2 arm (:0635fdd9).
//
// WHAT IT IS. A conservative-advancement contact-time estimate between one
// object treated as a POINT and one triangle face of another object's compact
// ledge. The "point" reading is not an inference from the name: the evaluator's
// own local point is set to the literal (0,0,0) at :0635df8b-:0635df9b, and
// Evaluate_0635d5b0 pushes it through FrameA with vmult4, so the tested point
// is exactly object A's frame origin. The plane it is tested against is
// carried by object B: the anchor is the feature edge's start vertex and the
// normal is that triangle's face normal, both expressed in B's local frame and
// transformed by FrameB inside the evaluator.
//
// ARGUMENT ORDER, from the PUSH order at the call site (:0635fdd2/:0635fdd7/
// :0635fdd8): the last PUSH is param_1, so param_1 is Mindist->FeatureB,
// param_2 is ObjectA's cache object, param_3 is &SideB. Note param_1 is the
// RAW tagged edge pointer -- the dispatcher's `AND EAX,0xfffffff0` at
// :0635fd76 is a separate temporary used only to find the containing ledge,
// and the value actually passed is the unmasked one saved at :0635fd29.
//
// CACHES. Retail builds both 0x5a8-byte caches on its own frame, running the
// MSVC vector-constructor-iterator over each one's 21-matrix tail first
// (:0635df18/:0635df53). Csys::Csys is `MOV EAX,ECX; RET` in full, so those
// 42 invocations are a runtime no-op and are not reproduced -- same reasoning
// as IVP_Sweep_Frame_Cache::Init's own note above. The ASYMMETRY between the
// two seeds is retail's and is transcribed literally: CacheA is seeded from
// param_2 DIRECTLY (:0635df25 reads the parameter slot, not through it),
// while CacheB is seeded from param_3->Source (:0635df40 `MOV ECX,[EBX+0x8]`).
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeFaceContact(
    const void *FeatureEdge, FIVP_Sweep_Cache_Source *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635df0c-:0635df34. local_5a8.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    // :0635df39-:0635df69. local_b50. Retail spills SideB->Source into
    // local_b9c at :0635df43 and reloads it at :0635df60 purely because the
    // vector-ctor loop clobbers the register in between.
    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635df6e-:0635df81. `(*Edge & 0xffff) * 0x10 + SideB->Points`. Retail
    // indexes SideB->Points, the ALREADY-RESOLVED absolute point-array base,
    // not the ledge -- see FIVP_Sweep_Ledge_Side's comment. The two are equal
    // by construction; the literal form is kept.
    const IVP_Compact_Edge *const Edge = static_cast<const IVP_Compact_Edge *>(FeatureEdge);
    const IVP_U_Float_Point *const PointArray =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const Vertex = PointArray + Edge->GetStartPointIndex();

    IVP_Sweep_PointPlane_Evaluator Evaluator;

    // :0635df8b-:0635df9b. Three separate stores of the integer 0, i.e. the
    // three float components; +0x20 deliberately left alone.
    Evaluator.PointA.set(0.0f, 0.0f, 0.0f);

    // :0635dfa3-:0635dfc3. Retail interleaves this with the pushes for the
    // call below: it loads the vertex's y and x onto the x87 stack and copies
    // z as raw bits through EDX/EAX, then stores x, y and z into +0x34, +0x38
    // and +0x3c.
    Evaluator.Anchor.set(Vertex->X, Vertex->Y, Vertex->Z);

    // :0635dfc7 CALL FUN_0635b940(Edge, SideB->Ledge, &Evaluator.Normal),
    // cdecl, `ADD ESP,0xc`; then :0635dfd3 CALL FUN_06347ef0 with ECX =
    // &Evaluator.Normal. Retail ignores normize's bool return, so a
    // degenerate (zero-area) triangle leaves the normal unnormalized rather
    // than zeroed -- ivp_math.h's normize() behaves the same way.
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
        Edge, static_cast<const IVP_Compact_Ledge *>(SideB->Ledge), &Evaluator.Normal); // FUN_0635b940
    Evaluator.Normal.normize();

    // :0635dfd8-:0635dff7. `FLD [ESI+8]` then `FLD 1.0; FDIV` -- the
    // reciprocal is computed from the stored field, not recomputed from
    // ClosingBound, which matters if ClosingBound is zero (retail produces an
    // infinity here and does not guard it; neither does this).
    Evaluator.MaxRelativeSpeed = this->ClosingBound;
    Evaluator.IMaxRelativeSpeed = 1.0f / Evaluator.MaxRelativeSpeed;

    // :0635dffb-:0635e04f. All three scalars come off ONE x87 stack in the
    // order below, with the combined radius kept live in ST1 across the whole
    // sequence and popped unused at :0635e056.
    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);

    // `FLD [EAX+0x50]` -- IVP_Ledge_Mindist::CombinedRadius.
    const float CombinedRadius = LedgeMindist->CombinedRadius;

    // `MOV ECX,[EAX+0x14]; SHR ECX,0x16; AND ECX,0xff` then
    // `FLD [ECX*4 + DAT_0658aa18]`. Exactly the ramp index
    // ivp_mindist_limiter.cpp already derived for FUN_0634f320.
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // :0635e015-:0635e023. DAT_063a5460 is 0.5f -- raw .rdata bytes
    // 00 00 00 3f (Common.dll.txt:568776-568779), NOT the 1.0f that
    // DAT_063a53d0 four rows earlier holds. DAT_0658aa10 is the tolerance
    // block's own [0x00].
    const float InnerThreshold = CombinedRadius * 0.5f + SweepToleranceBase();

    // :0635e027-:0635e035. `FLD ST1; FADD [EAX+0x54]` -- the distance already
    // known at the start of the interval. SeparationGap is |Delta| minus the
    // combined radius, so this adds back to the raw separation the evaluator
    // itself measures.
    float KnownDistance = CombinedRadius + LedgeMindist->SeparationGap;

    // :0635e03d `FADD ST0,ST1` then :0635e04f `FSTP [ESP]` -- the outer
    // threshold is ramp PLUS radius, and it is the argument that lands at
    // [ESP+0], i.e. param_1.
    const float Threshold = Ramp + CombinedRadius;

    // :0635e052 `LEA ECX,[ESP+0x40]` is the evaluator (the receiver);
    // :0635dfef pushed `LEA EDX,[ESI+0x2c]`, this context's own out-time pair,
    // as the last argument. The six intervening PUSHes map, from the one
    // nearest [ESP+0] outward, to Threshold, InnerThreshold, NowBase,
    // NowOffset, NextBase, NextOffset, &CacheA, &CacheB, &KnownDistance.
    const int Hit = Evaluator.SearchContactTimeFromContact(
        Threshold, InnerThreshold,
        this->NowBase, this->NowOffset,
        this->NextBase, this->NextOffset,
        &CacheA, &CacheB, &KnownDistance,
        reinterpret_cast<IVP_Time *>(&this->OutEventBase));

    // :0635e05d-:0635e061. `TEST EAX,EAX; JZ; MOV [ESI+0x28],0x20`. The
    // dispatcher zeroed OutCode before calling (:0635fda8), so a miss leaves
    // it zero and this is the only writer of the 0x20 event code.
    if (Hit != 0)
    {
        this->OutCode = 0x20;
    }
}

// ============================================================================
// FUN_0635da70 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeFaceContact
// Common.dll.txt:449341-449699. Bytes 0x0635da70 through the `RET 0x10` at
// 0x0635de83 inclusive -- all 299 instructions read first to last for this
// pass, with the x87 stack simulated across both CALL sequences and every
// PUSH-induced re-basing of ESP resolved (the frame is `SUB ESP,0xc0c` plus
// four pushes, so Ghidra's constant is 0xc1c, and inside the second call
// sequence ESP moves nine more times before the CALL).
//
// Decomp-tracker item 3369. Called from LAB_0635fef0 at :0636002a.
//
// WHAT IT IS: two different sweep tests against the same pair of objects,
// run back to back, with the SECOND one repeated once per edge of a vertex
// fan.
//
//   Pass 1 (:0635db19-:0635dc2b) is the same point-vs-plane test item 3371
//   runs -- evaluator PTR_LAB_0635d5b0 -- except that the point is a real
//   ledge VERTEX of side A rather than the literal origin: +0x14 is loaded
//   from side A's own point array at :0635db21-:0635db52, where item 3371
//   stored three zeroes. Threshold, inner threshold and known distance are
//   built from the mindist exactly as in item 3371.
//
//   Pass 2 (:0635dc32-:0635de74) installs a SECOND evaluator,
//   PTR_LAB_0635da20, and walks the fan of edges around that same vertex,
//   testing each edge DIRECTION against the face normal. It is the rate test
//   that pass 1 cannot do: pass 1 asks "when do this vertex and that face
//   touch", pass 2 asks "which of the edges leaving this vertex is closing
//   fast enough to matter before the event time pass 1 just found".
//
// THE OUT-TIME IS ALSO THE INTERVAL END, and that is the structural point of
// the function. :0635dafd/:0635db04 seed this->OutEvent* from this->Next*, and
// from then on EVERY solver call reads its end-of-interval arguments back out
// of OutEvent* (through EDI = ESI+0x2c, :0635dbb7/:0635dbfd and :0635ddf3/
// :0635de08) and writes its result to the SAME pair. So each successful test
// narrows the window the remaining tests search, and the function as a whole
// returns the earliest contact any of them found. A transcription that gave
// the solver `NextBase/NextOffset` every time would still compile and would
// still find contacts -- it would just stop finding the earliest one.
//
// THE FAN WALK is `Cursor = Cursor->GetPrevEdge()->GetOppositeEdge()`
// (:0635de58-:0635de6e, and the same expression once before the loop at
// :0635dd20-:0635dd3b to pick the starting cursor). That is the standard
// compact-mesh rotation around a shared vertex, and the two DAT_ tables it
// goes through are the next/prev step tables resolved in this file's header.
// The loop terminates on `Cursor == FeatureEdgeA` tested AFTER the body
// (:0635de4f-:0635de56), so the hub edge itself is the last one processed,
// not the first.
//
// .rdata CONSTANTS used here, all read as raw bytes:
//   DAT_063a5460 = 00 00 00 3f              -> 0.5f
//   DAT_063a5464 = cd cc cc 3d              -> 0.1f   (Ghidra's listing
//                                              comment "= CDh" is one byte
//                                              and means nothing)
//   DAT_063a53d8 = ff e6 db 2e              -> 1e-10f (0x2EDBE6FF; exponent
//                                              0x5D, so 2^-34 * 1.7176)
//   DAT_063a53d0 = 00 00 80 3f              -> 1.0f
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeFaceContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635da81-:0635daa4 and :0635dab0-:0635dad1. Both caches are seeded from
    // their side's Source -- unlike item 3371, which takes its first source as
    // a bare parameter. The two vector-constructor-iterator calls in between
    // (FUN_06273bc0 over 21 * 0x40 bytes with Csys::Csys) are the same runtime
    // no-op documented on IVP_Sweep_Frame_Cache::Init.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635dad6-:0635db00, read through the caches' own Core field (+0x04 of
    // each 0x5a8-byte cache -- Ghidra's local_b4c and local_5a4 are CacheB+4
    // and CacheA+4, not separate locals). B is loaded first and A is added.
    //
    // NOTE ON THE FIELD NAME, recorded rather than silently accepted: this
    // port calls IVP_Core+0x148 `MaxSurfaceDeviation`. Here it is multiplied
    // by a time span to bound a DISTANCE (:0635dd48) and handed to the solver
    // as MaxRelativeSpeed, i.e. it is being used with units of speed. Either
    // the existing name is wrong or this is a second meaning for the field;
    // this lane did not settle it and is not renaming another file's member.
    const float SpeedBound =
        CacheB.Core->MaxSurfaceDeviation + CacheA.Core->MaxSurfaceDeviation;

    // :0635daf0-:0635db04. Seed the out-time with the end of the interval --
    // see this function's header comment.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635db07-:0635db17 and :0635db36-:0635db49. One vertex from each side,
    // each `(*Edge & 0xffff) * 0x10 + Side->Points`.
    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    // ---- Pass 1: vertex of A against the face carrying edge B ----------------
    IVP_Sweep_PointPlane_Evaluator VertexFace;

    // :0635db21-:0635db52 (+0x14) and :0635db59-:0635db82 (+0x34). Retail
    // interleaves the two vertex loads, carrying each one's z as raw integer
    // bits through a general register while x and y ride the x87 stack.
    VertexFace.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    VertexFace.Anchor.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635db89 / :0635db98. Side B's ledge, side B's edge.
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
        EdgeB, static_cast<const IVP_Compact_Ledge *>(SideB->Ledge), &VertexFace.Normal); // FUN_0635b940
    VertexFace.Normal.normize();

    // :0635db9d-:0635dbba.
    VertexFace.MaxRelativeSpeed = this->ClosingBound;
    VertexFace.IMaxRelativeSpeed = 1.0f / VertexFace.MaxRelativeSpeed;

    // :0635dbae-:0635dc16, the identical scalar block item 3371 uses.
    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);
    const float InnerThreshold = CombinedRadius * 0.5f + SweepToleranceBase();
    float KnownDistance = CombinedRadius + LedgeMindist->SeparationGap;
    const float Threshold = Ramp + CombinedRadius;

    // :0635dc22-:0635dc2b. End-of-interval arguments come from OutEvent*, not
    // from Next* -- at this point the two are still equal, but see below.
    if (VertexFace.SearchContactTimeFromContact(
            Threshold, InnerThreshold,
            this->NowBase, this->NowOffset,
            this->OutEventBase, this->OutEventOffset,
            &CacheA, &CacheB, &KnownDistance,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        this->OutCode = 0x20;
    }

    // ---- Pass 2: the edge fan around that same vertex of A -------------------
    IVP_Sweep_EdgeNormal_Evaluator EdgeNormal;

    // :0635dc32-:0635dc5d. Retail copies FOUR dwords, i.e. the whole
    // IVP_U_Point including its 4th float, which nothing ever wrote. The
    // destination's 4th dword (+0x30) has no reader: Evaluate_0635da20
    // (0635da20-0635da68) reads +0x14..+0x1c and hands +0x24 to the
    // rotation-only vmult (FUN_06348580, x/y/z). Copying the three live
    // components is therefore observably identical.
    EdgeNormal.Normal.set(&VertexFace.Normal);

    // :0635dc69-:0635dc96. FCOMPP of SeparationGap (ST0) against the ramp
    // entry (ST1); mask and jump together: `TEST AH,0x41` + `JNZ` takes the
    // branch that reloads SeparationGap, i.e. the jump fires when Gap <= Ramp
    // OR unordered. So this is min(Gap, Ramp), with Gap winning ties and NaN.
    const float SeparationGap = LedgeMindist->SeparationGap;
    const float GapOrRamp = !(SeparationGap > Ramp) ? SeparationGap : Ramp;

    // :0635dc96-:0635dcc5, x87 stack simulated; the whole chain stays on the
    // x87 stack and is rounded ONCE by the FSTP dword at :0635dccc. The
    // negation is retail's `FCHS` at :0635dca8 and applies to the bias term
    // alone, before the multiply -- not to the whole expression afterwards.
    //   -(ToleranceBias * ObjectB->core->DAngularLimitFactor)
    //      * (CombinedRadius * 0.1f + GapOrRamp) / Ramp
    const IVP_Core *const CoreB = SideB->Object->core;
    const double BiasTerm = -((double)(SweepToleranceBias()) * CoreB->DAngularLimitFactor);
    const float ApproachThreshold = (float)(
        BiasTerm * ((double)(CombinedRadius) * (double)(0.1f) + GapOrRamp) / Ramp);

    // :0635dcba-:0635dcd2 then :0635dcd7-:0635dcea. The face normal is carried
    // out of B's local frame into world space and then into A's local frame.
    // Both transforms are rotation-only, which is correct for a normal, and
    // both matrices are the caches' SOURCE frames (`Source + 0x1c`), not any
    // frame the solver later fills.
    IVP_U_Point WorldNormal;
    SideB->Source->Frame.vmult(&EdgeNormal.Normal, &WorldNormal);

    IVP_U_Point LocalNormalA;
    SideA->Source->Frame.vimult(&WorldNormal, &LocalNormalA);

    // :0635dcef-:0635dd2b. A different speed bound from pass 1's: the sum of
    // the two cores' own bounds plus 1e-10, not the context's ClosingBound.
    EdgeNormal.MaxRelativeSpeed = SpeedBound + 1e-10f;
    EdgeNormal.IMaxRelativeSpeed = 1.0f / EdgeNormal.MaxRelativeSpeed;

    // :0635dd2f-:0635dd4c. Computed AFTER pass 1's call, so it is bounded by
    // whatever event time pass 1 may have written. Extended chain, one FSTP
    // dword into local_c04 at :0635dd4c.
    const float MaxTravel = (float)(
        ((((double)(this->OutEventBase) - this->NowBase) + this->OutEventOffset)
         - this->NowOffset)
        * EdgeNormal.MaxRelativeSpeed);

    // :0635dd20-:0635dd3b.
    const IVP_Compact_Edge *Cursor = EdgeA->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        // LAB_0635dd50, :0635dd50-:0635dd8b. The vector from the hub vertex to
        // the far end of this fan edge. The far end is the NEXT edge's start
        // point, which is what `[NEXT_TABLE[tag] + Cursor]` loads -- note it
        // loads the next edge's DWORD, not its address.
        const IVP_U_Float_Point *const FarPoint =
            PointsA + Cursor->GetNextEdge()->GetStartPointIndex();

        // :0635dd69-:0635dd89. x and y are FSTP dword'd (local_c00 /
        // local_bfc); z is FST dword'd into local_bf8 but its UNROUNDED
        // difference stays in ST0 and is what the projection and the squared
        // length below multiply (FMUL ST1 at :0635dd91, FMUL ST3 at :0635ddbf).
        const float DeltaX = FarPoint->X - VertexA->X;
        const float DeltaY = FarPoint->Y - VertexA->Y;
        const double DeltaZExt = (double)(FarPoint->Z) - VertexA->Z;
        const float DeltaZ = (float)(DeltaZExt);

        // :0635dd8d-:0635dda7, in retail's own association order: the z and y
        // terms are summed first, then the x term is added to that; FSTP dword
        // local_c08 at :0635dda7.
        const float Projection = (float)(
            ((double)(LocalNormalA.k[2]) * DeltaZExt
             + (double)(LocalNormalA.k[1]) * DeltaY)
            + (double)(DeltaX) * LocalNormalA.k[0]);

        // :0635ddab-:0635ddc3, also in retail's order: (x*x + y*y) + z*z,
        // FSTP dword as FUN_06347df0's argument at :0635ddc3.
        const float QuadLength = (float)(
            ((double)(DeltaX) * DeltaX + (double)(DeltaY) * DeltaY)
            + DeltaZExt * DeltaZExt);

        // :0635ddca CALL FUN_06347df0: FLD/FSQRT/FDIVR 1.0f, result returned
        // unrounded in ST0 and kept there for the direction below.
        const double InverseLength = IVP_InvSqrtFloat(QuadLength);

        // :0635ddcf-:0635ddd8. FLD ST0 / FMUL local_c08 / FST dword local_c08
        // -- the stored copy's ADDRESS is handed to the solver below as its
        // known-distance argument; the compare uses the unrounded product.
        const double AlignmentExt = InverseLength * Projection;
        float Alignment = (float)(AlignmentExt);

        // :0635dddc FCOMP against MaxTravel / :0635dde2 TEST AH,0x1 / JZ
        // LAB_0635de49. Mask and jump together: C0 alone, and the JUMP (skip
        // the test entirely) is taken when C0 is CLEAR, i.e. when Alignment is
        // >= MaxTravel. The fall-through, which runs the sweep, is strictly
        // less OR unordered.
        if (!(AlignmentExt >= MaxTravel))
        {
            // :0635dde7-:0635de31. The unit edge direction, component by
            // component, into the evaluator's +0x14: each FLD of the ROUNDED
            // delta (local_c00 / local_bfc / local_bf8) times the unrounded
            // InverseLength in ST1, FSTP dword.
            EdgeNormal.Direction.set((float)(DeltaX * InverseLength),
                                     (float)(DeltaY * InverseLength),
                                     (float)(DeltaZ * InverseLength));

            // :0635de37. The OTHER solver entry point -- SearchContactTime,
            // not SearchContactTimeFromContact -- with a literal 0 start slot
            // (:0635de15) and, again, OutEvent* as the end of the interval.
            if (EdgeNormal.SearchContactTime(
                    ApproachThreshold,
                    this->NowBase, this->NowOffset,
                    this->OutEventBase, this->OutEventOffset,
                    0,
                    &CacheA, &CacheB, &Alignment,
                    reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
            {
                // :0635de40. A DIFFERENT event code from pass 1's 0x20.
                this->OutCode = 0x21;
            }
        }

        // :0635de4b-:0635de56. Tested after the body, so the hub edge is
        // processed on the iteration that closes the fan.
        if (Cursor == EdgeA)
        {
            break;
        }

        // :0635de58-:0635de72. Retail reuses the tag it saved at :0635dd71,
        // which is this same cursor's own `& 0xc`; the expression is
        // identical.
        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635e1d0 -- IVP_Sweep_Estimator_Context::EstimateLedgeEdgeVsLedgeEdgeContact
// Common.dll.txt:450051-450495. Bytes 0x0635e1d0 through the `RET 0x10` at
// 0x0635e77e inclusive -- ALL 361 instructions read first to last for this
// pass, with the x87 stack simulated through each of the four load/store
// interleavings and every PUSH-induced re-basing of ESP resolved back to
// frame+N (the frame is ESP_entry-0xc7c; see the evaluator block above).
//
// Decomp-tracker item 3374. Its single XREF is :0635fff4, inside dispatcher
// LAB_0635fef0 -- the same dispatcher that reaches item 3369.
//
// WHAT IT IS: the EDGE-versus-EDGE arm of the sweep. Item 3369 handles a
// vertex of A against a face of B; this one handles one edge of A against one
// edge of B, and it runs FOUR distinct tests, in this order:
//
//   1. the skew-line edge-edge separation itself (evaluator PTR_LAB_0635e080),
//      event code 0x40;
//   2. the parallel-edge degeneracy guard (PTR_LAB_0635e160), which measures
//      |dirA x dirB|^2 -- exactly the quantity test 1 divides by -- and so
//      catches the case where test 1's answer is meaningless, event code 0x41;
//   3. four "does this edge direction point into that face" tests
//      (PTR_LAB_0635d6d0) over the 2x2 product of {edge, its opposite} on each
//      side, event code 0x42.
//
// THREE PREMISES OF THIS LANE'S BRIEF THAT THE LISTING CONTRADICTS, recorded
// because each one would have produced wrong code:
//
//   (a) "the scalar block (InnerThreshold = Radius*0.5 + ToleranceBase;
//       KnownDistance = Radius + SeparationGap; Threshold = Ramp + Radius)
//       that both of them share and this one almost certainly shares too."
//       IT DOES NOT. This driver never reads IVP_Ledge_Mindist::CombinedRadius
//       (+0x50) or ::SeparationGap (+0x54) at all -- grep the byte range, the
//       only Mindist offsets touched are +0x14 (StateFlags, :0635e463) and
//       +0x60/+0x64/+0x68 (SeparatingNormal, :0635e417-:0635e427). Its outer
//       threshold is the RAW ramp entry with nothing added (:0635e474), its
//       inner threshold is the RAW tolerance base (:0635e49c), and all three
//       of its solver calls pass a NULL known-distance pointer (the literal
//       `PUSH 0x0` at :0635e4f9, :0635e547 and :0635e730). The radius-like
//       quantity it does build is a different one entirely: the sum of the two
//       cores' +0x148 (:0635e277-:0635e27d), which is what item 3369 calls
//       SpeedBound, not what it calls CombinedRadius.
//
//   (b) "a census by lane w16-ivp asserts FUN_0634d8d0 = IVP_U_Matrix::vmult4
//       and FUN_0634d9d0 = vmult ... Item 3378 is the one expected to call
//       them." Not this item's problem -- FUN_0635e1d0 calls NEITHER. Its only
//       matrix transform is FUN_06348580 (twice, :0635e3d9 and :0635e3f8), the
//       already-verified IVP_U_Matrix3::vmult. The caution is left standing
//       for item 3378 and was not discharged here.
//
//   (c) the header's own long-standing claim that the drivers are what walk the
//       `& 0xc` next/prev tables. This one does use them (:0635e2a3 and
//       :0635e2df) but for something simpler than a fan walk: just the far end
//       of each of the two edges. The rotation this driver does instead is
//       `GetOppositeEdge()`, at :0635e5bc-:0635e5c8 and :0635e5e5-:0635e5f2 --
//       `((int32_t)(Edge->Data << 1) >> 17)` added to the edge as a dword
//       index, which is IVP_Compact_Edge::GetOppositeIndex() term for term.
//
// THE OUT-TIME IS AGAIN THE INTERVAL END. :0635e290/:0635e297 seed OutEvent*
// from Next*, and from there every one of the six solver calls (1 + 1 + 4)
// takes its end-of-interval pair out of OutEvent* and writes its result back
// to the same pair, so each hit narrows the window the later tests search.
// Passing Next* to the later calls would still find contacts; it would just
// stop finding the earliest one.
//
// .rdata CONSTANTS, read as RAW BYTES (Ghidra's inline `= FFh` / `= ??`
// comments on three of these are wrong or empty):
//   DAT_063a53d0 = 00 00 80 3f  -> 1.0f
//   DAT_063a53d4 = 00 00 00 00  -> 0.0f
//   DAT_063a53d8 = ff e6 db 2e  -> 1e-10f   (Ghidra prints "= FFh")
//   DAT_063a5498 = 00 00 80 bf  -> -1.0f    (Ghidra prints nothing)
// and the .data ones are the same uninitialized tolerance block items 3369 and
// 3371 already reach: DAT_0658aa10 == block[0x00], DAT_0658aa18 == &block[2],
// DAT_0658ab34 == block[0x49].
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeEdgeVsLedgeEdgeContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635e1d9-:0635e20f and :0635e214-:0635e258. Both caches seeded from
    // their own side's Source, as in item 3369. The two 21-iteration
    // Csys::Csys loops in between (:0635e1f6 and :0635e231 -- written out
    // inline here rather than through FUN_06273bc0) are the same runtime no-op
    // documented on IVP_Sweep_Frame_Cache::Init: Csys::Csys is `MOV EAX,ECX;
    // RET` in full.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635e263. The vfptr store that identifies evaluator 1.
    IVP_Sweep_EdgeEdge_Evaluator EdgeEdge;

    // :0635e25d-:0635e293. A's core is loaded first and B's is added to it --
    // the opposite association order from item 3369's identical-looking sum,
    // which loads B first. Kept as retail spells it.
    //
    // Same naming caveat item 3369 recorded: this port calls IVP_Core+0x148
    // MaxSurfaceDeviation, and here the sum is used as a relative-speed bound.
    // Not renamed by this lane.
    const float SpeedBound =
        SideA->Object->core->MaxSurfaceDeviation + SideB->Object->core->MaxSurfaceDeviation;

    // :0635e283-:0635e297. Seed the out-time with the end of the interval.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635e29a-:0635e31d. Both edges' start and far vertices. The far vertex
    // is the NEXT edge's start point -- retail loads the next edge's DWORD
    // through the table (`MOV EDX,[EDX + DAT_063ee4e0]; MOV EAX,[EDX + EAX]`),
    // not its address, and masks 0xffff off that.
    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);

    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexA =
        PointsA + EdgeA->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexB =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    // :0635e2b6-:0635e326. Retail computes y first, then z (spilled through a
    // general register as raw bits), then x, and stores them x/y/z. The
    // normalize is `CALL FUN_06347ef0` with ECX = &field, i.e. in place, and
    // retail ignores its bool return -- a degenerate edge is left unnormalized
    // rather than zeroed, exactly as normize() behaves here.
    EdgeEdge.DirectionA.set(FarVertexA->X - VertexA->X,
                            FarVertexA->Y - VertexA->Y,
                            FarVertexA->Z - VertexA->Z);
    EdgeEdge.DirectionA.normize();

    // :0635e32b-:0635e35f.
    EdgeEdge.DirectionB.set(FarVertexB->X - VertexB->X,
                            FarVertexB->Y - VertexB->Y,
                            FarVertexB->Z - VertexB->Z);
    EdgeEdge.DirectionB.normize();

    // :0635e364-:0635e37c and :0635e383-:0635e3ce. Both start vertices, raw,
    // in their own object's local space.
    EdgeEdge.PositionA.set(VertexA->X, VertexA->Y, VertexA->Z);
    EdgeEdge.PositionB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635e3ae-:0635e3d5. As in items 3369/3371 the reciprocal is taken from
    // the STORED field, not recomputed, and there is no zero guard.
    EdgeEdge.MaxRelativeSpeed = this->ClosingBound;
    EdgeEdge.IMaxRelativeSpeed = 1.0f / EdgeEdge.MaxRelativeSpeed;

    // :0635e3c8-:0635e3f8. Two `CALL FUN_06348580` == IVP_U_Matrix3::vmult,
    // both on a cache SOURCE frame (`Source + 0x1c`), rotation only, which is
    // correct for directions. Argument order taken from which PUSH lands at
    // [ESP+0]: at :0635e3d9 that is &DirectionB (pushed second, :0635e3c7), so
    // DirectionB is p_in and frame+0x2c is p_out; likewise at :0635e3f8.
    IVP_U_Point WorldDirectionB;
    SideB->Source->Frame.vmult(&EdgeEdge.DirectionB, &WorldDirectionB);

    IVP_U_Point WorldDirectionA;
    SideA->Source->Frame.vmult(&EdgeEdge.DirectionA, &WorldDirectionA);

    // :0635e407-:0635e40b. FUN_0628bed0(this = out, param_1, param_2) computes
    // `out = param_1 x param_2` with the products on the x87 stack and one
    // float rounding per component (IVP_CalcCrossProduct); &WorldDirectionA is
    // pushed second (:0635e406) and therefore sits at [ESP+0], so it is
    // param_1.
    IVP_U_Point Axis;
    IVP_CalcCrossProduct(&Axis, &WorldDirectionA, &WorldDirectionB);

    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);

    // :0635e410-:0635e42c, in retail's own association order: the y and z terms
    // are summed first, the x term is added to that, and the FCHS negates the
    // whole sum -- all on the x87 stack, compared unrounded (no store).
    // Mindist +0x60/+0x64/+0x68 is IVP_Ledge_Mindist's SeparatingNormal,
    // already derived independently in ivp_mindist.h.
    const double NegatedAlignment =
        -(((double)(Axis.k[1]) * LedgeMindist->SeparatingNormal.y
           + (double)(Axis.k[2]) * LedgeMindist->SeparatingNormal.z)
          + (double)(Axis.k[0]) * LedgeMindist->SeparatingNormal.x);

    // :0635e42e-:0635e463. MASK AND JUMP TOGETHER: `FCOMP 0.0f` (DAT_063a53d4
    // is raw 00 00 00 00) then `TEST AH,0x41` (C0|C3) + `JNZ`, so the JUMP is
    // taken when NegatedAlignment <= 0.0f and the FALL-THROUGH is strictly
    // greater. The fall-through stores 0 / -1.0f; the jump target (also taken
    // when unordered) stores 1 / +1.0f.
    //
    // Restated on the un-negated quantity: SwapFlag and Scale both follow the
    // sign of `dot(Axis, SeparatingNormal)` -- negative gives (0, -1.0f),
    // non-negative gives (1, +1.0f). Scale is evaluator 1's +0x54 multiplier,
    // so its job is to make the skew-line separation come out positive on the
    // side the mindist already believes the objects are separating towards;
    // SwapFlag re-uses the same decision to orient the four face tests below.
    int SwapFlag;
    if (NegatedAlignment > 0.0)
    {
        SwapFlag = 0;
        EdgeEdge.Scale = -1.0f;
    }
    else
    {
        SwapFlag = 1;
        EdgeEdge.Scale = 1.0f;
    }

    // :0635e463-:0635e474. The SAME ramp index item 3371 derives, but here the
    // ramp entry is the outer threshold on its own -- nothing is added to it.
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // :0635e466-:0635e4b1. Ten pushes; mapping them from [ESP+0] outward at the
    // CALL gives Threshold, InnerThreshold, NowBase, NowOffset, OutEventBase,
    // OutEventOffset, &CacheA, &CacheB, null, &OutEvent. The null
    // known-distance (`PUSH 0x0`, :0635e4f9 is the matching one for the next
    // call; this one is :0635e46a) is the substantive difference from items
    // 3369/3371, which both pass the address of a real distance.
    if (EdgeEdge.SearchContactTimeFromContact(
            Ramp, SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->OutEventBase, this->OutEventOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635e4ba-:0635e4cb. `FILD result; FCOMP 0.0f; TEST AH,0x40` (C3,
        // equality) `+ JNZ` skips the store when the result IS zero, so the
        // store runs on a non-zero result.
        this->OutCode = 0x40;
    }

    // ---- Test 2: the parallel-edge degeneracy guard --------------------------
    IVP_Sweep_EdgeParallel_Evaluator Parallel;

    // :0635e4d2-:0635e52f. Straight copies of evaluator 1's two directions,
    // component by component, into evaluator 2's +0x14 and +0x24.
    Parallel.DirectionA.set(&EdgeEdge.DirectionA);
    Parallel.DirectionB.set(&EdgeEdge.DirectionB);

    // :0635e536-:0635e581. `FADD ST0,ST0` doubles the core-deviation sum before
    // the epsilon is added; the reciprocal again comes off the stored field.
    Parallel.MaxRelativeSpeed = (SpeedBound + SpeedBound) + 1e-10f;
    Parallel.IMaxRelativeSpeed = 1.0f / Parallel.MaxRelativeSpeed;

    // :0635e56a-:0635e588. The OTHER solver entry point, with a literal
    // 1e-10f threshold pushed as the immediate 0x2edbe6ff (the same bit
    // pattern DAT_063a53d8 holds), a zero start slot, a null known distance,
    // and once more OutEvent* as both the interval end and the output.
    if (Parallel.SearchContactTime(
            1e-10f,
            this->NowBase, this->NowOffset,
            this->OutEventBase, this->OutEventOffset,
            0,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635e5a2.
        this->OutCode = 0x41;
    }

    // ---- Test 3: the four edge-direction-versus-face tests -------------------
    IVP_Sweep_EdgeFace_Evaluator EdgeFace;

    // :0635e5a9-:0635e5d5 and :0635e67e-:0635e689. Note this bound is the
    // SINGLE core-deviation sum plus epsilon, not the doubled one test 2 uses.
    EdgeFace.MaxRelativeSpeed = SpeedBound + 1e-10f;
    EdgeFace.IMaxRelativeSpeed = 1.0f / EdgeFace.MaxRelativeSpeed;

    // :0635e5bc-:0635e5c8 and :0635e5e5-:0635e5f2.
    const IVP_Compact_Edge *const OppositeA = EdgeA->GetOppositeEdge();
    const IVP_Compact_Edge *const OppositeB = EdgeB->GetOppositeEdge();

    // :0635e5f5-:0635e67a. Retail materializes six parallel four-entry tables
    // on its frame and then walks them with TWO cursors in OPPOSITE
    // directions: ESI counts up 0,4,8,0xc from one base and EBX counts DOWN
    // 0,-4,-8,-0xc from another (:0635e765-:0635e76e). Four of the tables are
    // read through ESI and two through EBX, which is why the Side and
    // second-cache tables below look reversed relative to the others -- they
    // are stored ascending and read descending. They are written out here in
    // the order the loop consumes them.
    //
    // The pairing is self-consistent and that is the check on it: on iteration
    // i the direction handed to the evaluator's +0x14 belongs to the object
    // whose cache is the FIRST solver argument, and the face normal handed to
    // +0x24 is built from the ledge of the side whose cache is the SECOND --
    // which is exactly what Evaluate_0635d6d0 needs, since it pushes +0x14
    // through FrameA and +0x24 through FrameB.
    const int UseRawDirection[4] = { SwapFlag, 1 - SwapFlag, SwapFlag, 1 - SwapFlag };
    const IVP_U_Point *const FanDirection[4] = {
        &EdgeEdge.DirectionB, &EdgeEdge.DirectionB,
        &EdgeEdge.DirectionA, &EdgeEdge.DirectionA };
    const IVP_Compact_Edge *const FanEdge[4] = { OppositeA, EdgeA, OppositeB, EdgeB };
    FIVP_Sweep_Ledge_Side *const FanSide[4] = { SideA, SideA, SideB, SideB };
    IVP_Sweep_Frame_Cache *const FanFirstCache[4] = { &CacheB, &CacheB, &CacheA, &CacheA };
    IVP_Sweep_Frame_Cache *const FanSecondCache[4] = { &CacheA, &CacheA, &CacheB, &CacheB };

    for (int FanIndex = 0; FanIndex < 4; ++FanIndex)
    {
        const IVP_U_Point *const Direction = FanDirection[FanIndex];

        // :0635e692-:0635e6e7. `TEST EAX,EAX; JNZ` -- the JUMP (the raw copy)
        // is taken when the flag is NON-zero, so it is the ZERO flag that
        // negates. The negation is three separate `FMUL DAT_063a5498` with
        // DAT_063a5498 == raw 00 00 80 bf == -1.0f, not a sign flip, so it is
        // written as a multiply here too (it differs from a sign flip on a
        // negative zero).
        if (UseRawDirection[FanIndex] != 0)
        {
            EdgeFace.Direction.set(Direction->k[0], Direction->k[1], Direction->k[2]);
        }
        else
        {
            EdgeFace.Direction.set(Direction->k[0] * -1.0f,
                                   Direction->k[1] * -1.0f,
                                   Direction->k[2] * -1.0f);
        }

        // :0635e6e9-:0635e714. cdecl `FUN_0635b940(Edge, Side->Ledge,
        // &Normal)` then an in-place normize, the same pair items 3369 and
        // 3371 run. Note the ledge comes from the SIDE record, so on the first
        // two iterations it is A's ledge even though the direction is B's --
        // that asymmetry is the whole point of the test.
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
            FanEdge[FanIndex], static_cast<const IVP_Compact_Ledge *>(FanSide[FanIndex]->Ledge),
            &EdgeFace.Normal); // FUN_0635b940
        EdgeFace.Normal.normize();

        // :0635e719-:0635e738. `FLD DAT_0658ab34; FMUL [Core+0x48]; FCHS`,
        // where Core is the FIRST cache's own +0x04. The FCHS applies to the
        // product, and the product is the whole threshold -- unlike item
        // 3369's structurally similar expression, nothing further is
        // multiplied or divided into it here.
        const float Threshold =
            -(SweepToleranceBias() * FanFirstCache[FanIndex]->Core->DAngularLimitFactor);

        // :0635e73c-:0635e755. Ten pushes again; zero start slot, null known
        // distance, OutEvent* as interval end and as output.
        if (EdgeFace.SearchContactTime(
                Threshold,
                this->NowBase, this->NowOffset,
                this->OutEventBase, this->OutEventOffset,
                0,
                FanFirstCache[FanIndex], FanSecondCache[FanIndex], 0,
                reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
        {
            // :0635e75e. A plain `TEST EAX,EAX; JZ` here, not the FILD/FCOMP
            // dance the first two tests use -- same meaning.
            this->OutCode = 0x42;
        }
    }
}

// ============================================================================
// FUN_0635f180 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeEdgeContact
// Common.dll.txt:451361-451925. Bytes 0x0635f180 through the `RET 0x10` at
// 0x0635f8bb inclusive -- ALL 494 instructions read first to last for this
// pass. The frame is `SUB ESP,0xc28` plus four pushes, so the settled frame
// pointer is ESP_entry-0xc38 and that is the 0xc38 in Ghidra's spellings;
// every `[ESP + local_XXX + 0xc3c/0xc40/0xc4c/0xc54]` variant is a spelling
// taken at a DIFFERENT push depth and was resolved back to frame+(0xc38-XXX)
// individually. Getting that wrong is how the evaluator fields stop lining up.
//
// Decomp-tracker item 3378, the largest driver in the family. Its one XREF is
// :06360045, dispatcher LAB_0635fef0.
//
// WHAT IT IS: one VERTEX of side A against one EDGE of side B, in three
// passes that reuse a single evaluator slot:
//
//   Pass 1 (:0635f1e6-:0635f43a), evaluator PTR_LAB_0635d950 / item 3367 --
//     the clamped moment test. Event code 0x30.
//   Pass 2 (:0635f441-:0635f64a), evaluator PTR_LAB_0635d5b0 / item 3362,
//     run TWICE, over B's edge and its opposite: the vertex against the plane
//     that stands on that edge, perpendicular to its own face. Code 0x31.
//   Pass 3 (:0635f650-:0635f8ac), evaluator PTR_LAB_0635d830 / item 3366,
//     run once per edge of the fan around vertex A: edge-vs-edge separation
//     between each fan edge and B's edge. Code 0x32.
//
// The three passes are strictly ordered and each one's interval end is
// narrowed by whatever the previous one wrote -- see the aliasing note below.
//
// FIVE THINGS A TRANSCRIPTION GETS WRONG IF IT GUESSES:
//
//   1. FUN_0634d8d0 AND FUN_0634d9d0 DO NOT TAKE A MATRIX. This lane's brief
//      warned that the w16-ivp census calls them vmult4 and vmult and had not
//      been re-derived. Both bodies were read in full this pass
//      (Common.dll.txt:422762-422810 for 0x0634d8d0-0x0634d964, and
//      422890-422917 for 0x0634d9d0-0x0634da27) and the census is RIGHT ABOUT
//      THE OPERATION -- 0634d8d0 is rows-dot-point PLUS the translation
//      (`FADD [ECX+0x4c/0x50/0x54]` at :0634d950/:0634d939/:0634d945, no
//      transpose), i.e. vmult4; 0634d9d0 is the same without the translation,
//      i.e. vmult. But it is WRONG BY OMISSION about the receiver, and the
//      omission is load-bearing: every matrix element is read at ECX+0x1c and
//      up (+0x1c/+0x20/+0x24, +0x2c/+0x30/+0x34, +0x3c/+0x40/+0x44,
//      +0x4c/+0x50/+0x54), i.e. the matrix sits at `this + 0x1c`, NOT at
//      `this`. Passing the matrix where retail passes the enclosing object
//      would read 0x1c bytes too early. At every call site here ECX is
//      `Side->Source` (:0635f32a, :0635f343, :0635f34b, :0635f3c8), and
//      FIVP_Sweep_Cache_Source::Frame is at +0x1c, so the port spells them
//      `Source->Frame.vmult4/vmult`.
//      A third member of the same family sits between them and IS used here:
//      FUN_0634d970 (:0634d970-:0634d9c7, also read in full) computes the
//      TRANSPOSED rotation -- out.x = M00*px + M10*py + M20*pz -- i.e.
//      vimult, again on the matrix at this+0x1c.
//      Incidental: that all three take the object and not the matrix is real
//      evidence for the IVP_Cache_Object reading of FIVP_Sweep_Cache_Source
//      that this file's header deliberately declined to assert. Still not
//      asserted here; recorded as evidence, not as a rename.
//
//   2. DAT_063a56f8 IS NEGATIVE. Raw bytes 9a 99 99 be == 0xBE99999A ==
//      -0.3f. Ghidra's listing comment says "= 9Ah", one byte. It is one bit
//      away from DAT_063a5668 (9a 99 99 3f == +1.2f), which this same file
//      already uses elsewhere, so a reader who pattern-matches the first three
//      bytes gets the sign AND the magnitude wrong and pass 3's threshold
//      comes out +1.2x instead of -0.3x.
//
//   3. PASS 1 PASSES Next*, NOT OutEvent*. :0635f406 and :0635f3f1 read
//      [ESI+0x20]/[ESI+0x24]. Passes 2 and 3 read [EBP]/[EBP+4] with EBP ==
//      ESI+0x2c (:0635f5e5/:0635f600 and :0635f856/:0635f848), i.e. OutEvent*.
//      The two are equal when pass 1 runs, because :0635f207/:0635f21f seed
//      OutEvent from Next -- so this is not a behavioural difference today,
//      but it is what retail wrote and it is transcribed as written rather
//      than normalized to one form.
//
//   4. THE TWO CORES ARE MEASURED DIFFERENTLY (:0635f686-:0635f6f5). Side A's
//      term uses the LIVE length of its own feature vertex (`MOV ECX,EBX;
//      CALL FUN_06347e60`, with EBX still pointing at that vertex), while side
//      B's uses the cached IVP_Core::BoundingRadius at +0x14c. Symmetrizing
//      that -- using BoundingRadius for both -- would loosen A's bound to the
//      whole object and change which contact the search finds first.
//
//   5. +0x0c OF THE CONTEXT IS LIVE HERE. Items 3369, 3371 and 3374 never read
//      it; this one uses it as pass 2's whole relative-speed bound
//      (:0635f441) and again as a speed multiplier in pass 3's span
//      (:0635f666). The header's name MarginSpan is kept, but note it is used
//      with units of speed in both places.
//
// .rdata CONSTANTS, read as RAW BYTES:
//   DAT_063a53d0 = 00 00 80 3f -> 1.0f
//   DAT_063a53fc = 66 66 66 3f -> 0.9f    (Ghidra prints "= 66h  f")
//   DAT_063a545c = a6 9b 44 3b -> 0.003f  (Ghidra prints "= A6h")
//   DAT_063a5460 = 00 00 00 3f -> 0.5f
//   DAT_063a56f8 = 9a 99 99 be -> -0.3f   (see point 2 above)
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeEdgeContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635f188-:0635f1e1. Same pair of caches as every other driver; the two
    // `FUN_06273bc0(ptr, 0x40, 0x15, Csys::Csys)` vector-constructor-iterator
    // calls are the documented runtime no-op.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635f1e6-:0635f218. A's core is loaded first, B's added -- and BOTH are
    // spilled to the frame (:0635f211 and :0635f226) because pass 3, 1200
    // bytes of code later, reads them back rather than re-walking
    // Object->core. The spills are why this port keeps named locals for them.
    const IVP_Core *const CoreA = SideA->Object->core;
    const IVP_Core *const CoreB = SideB->Object->core;
    const float SpeedBound = CoreA->MaxSurfaceDeviation + CoreB->MaxSurfaceDeviation;

    // :0635f204-:0635f21f. Seed the out-time with the end of the interval.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635f222-:0635f296. A's feature vertex, B's feature vertex, and the far
    // end of B's edge. Retail saves `&NextLut[EdgeB & 0xc]` into the frame at
    // :0635f265 and reuses it in pass 3 at :0635f6e0 -- the same next-edge
    // step, spelled once.
    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);

    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexB =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    // ---- Pass 1: the clamped moment test -------------------------------------
    // :0635f26b installs PTR_LAB_0635d950.
    IVP_Sweep_VertexEdgeMoment_Evaluator Moment;

    // :0635f278-:0635f2a9.
    Moment.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    Moment.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635f2ad-:0635f2d5. Unit edge direction of B, in B's local frame.
    Moment.DirectionB.set(FarVertexB->X - VertexB->X,
                          FarVertexB->Y - VertexB->Y,
                          FarVertexB->Z - VertexB->Z);
    Moment.DirectionB.normize();

    // :0635f2da-:0635f2ee.
    Moment.MaxRelativeSpeed = this->ClosingBound;
    Moment.IMaxRelativeSpeed = 1.0f / Moment.MaxRelativeSpeed;

    // :0635f2f2-:0635f313.
    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // :0635f31f-:0635f32d. DAT_063a5460 is raw 00 00 00 3f == 0.5f. This lands
    // in the evaluator's +0x14 SCALAR slot, which is the whole reason item
    // 3367's points start at +0x18.
    Moment.Bias = (Ramp + CombinedRadius) * 0.5f;

    // :0635f317-:0635f3b6. Both feature vertices into world space (vmult4,
    // translation included), B's edge direction into world space rotation-only
    // (vmult), then the moment of the separation about that edge.
    // Argument order for FUN_0628bed0 taken from which PUSH lands at [ESP+0]:
    // &Separation is pushed second (:0635f38b) so it is param_1.
    IVP_U_Point WorldVertexA;
    SideA->Source->Frame.vmult4(&Moment.PointA, &WorldVertexA);

    IVP_U_Point WorldVertexB;
    SideB->Source->Frame.vmult4(&Moment.PointB, &WorldVertexB);

    IVP_U_Point WorldDirectionB;
    SideB->Source->Frame.vmult(&Moment.DirectionB, &WorldDirectionB);

    // :0635f360-:0635f3af, component by component in retail's x/y/z order.
    IVP_U_Point Separation;
    Separation.set(WorldVertexB.k[0] - WorldVertexA.k[0],
                   WorldVertexB.k[1] - WorldVertexA.k[1],
                   WorldVertexB.k[2] - WorldVertexA.k[2]);

    // :0635f3b6 FUN_0628bed0 (out = local_b60): x87 products, one float
    // rounding per component -- IVP_CalcCrossProduct.
    IVP_U_Point WorldMoment;
    IVP_CalcCrossProduct(&WorldMoment, &Separation, &WorldDirectionB);

    // :0635f3c8-:0635f3d4. Back into B's local frame through the TRANSPOSED
    // rotation (FUN_0634d970 == vimult on Source->Frame), then normalized in
    // place with retail ignoring the bool return.
    SideB->Source->Frame.vimult(&WorldMoment, &Moment.MeasureB);
    Moment.MeasureB.normize();

    // :0635f3d9-:0635f431. Ten pushes; from [ESP+0] outward they are
    // Threshold, InnerThreshold, NowBase, NowOffset, NextBase, NextOffset,
    // &CacheA, &CacheB, null, &OutEvent. DAT_063a53fc is raw 66 66 66 3f ==
    // 0.9f -- NOT the 0.5f that items 3369/3371 multiply their radius by, so
    // this is a different inner threshold, not the family's shared one.
    // Note Next*, not OutEvent* -- see point 3 of the header comment.
    if (Moment.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.9f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635f43a.
        this->OutCode = 0x30;
    }

    // ---- Pass 2: the vertex against B's two edge-side planes -----------------
    // :0635f471 rebinds the slot to PTR_LAB_0635d5b0 -- the same concrete
    // class item 3371 uses, so the existing evaluator type is reused.
    IVP_Sweep_PointPlane_Evaluator EdgePlane;

    // :0635f441-:0635f48c. The relative-speed bound is the context's +0x0c,
    // not ClosingBound.
    EdgePlane.MaxRelativeSpeed = this->MarginSpan;
    EdgePlane.IMaxRelativeSpeed = 1.0f / EdgePlane.MaxRelativeSpeed;

    // :0635f490-:0635f4a0. Set once, outside the loop.
    EdgePlane.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);

    // :0635f463/:0635f479/:0635f484. A two-entry array on the frame plus a
    // pointer cursor and a counter of 2, walked by :0635f636-:0635f64a.
    const IVP_Compact_Edge *const PlaneEdge[2] = { EdgeB, EdgeB->GetOppositeEdge() };

    for (int PlaneIndex = 0; PlaneIndex < 2; ++PlaneIndex)
    {
        const IVP_Compact_Edge *const Edge = PlaneEdge[PlaneIndex];

        // :0635f4b3-:0635f511. All three points of this edge's own triangle.
        const IVP_U_Float_Point *const Cur = PointsB + Edge->GetStartPointIndex();
        const IVP_U_Float_Point *const Next =
            PointsB + Edge->GetNextEdge()->GetStartPointIndex();
        const IVP_U_Float_Point *const Prev =
            PointsB + Edge->GetPrevEdge()->GetStartPointIndex();

        // :0635f4d1-:0635f525. The edge vector, UNNORMALIZED -- there is no
        // normize call on it, unlike every other direction in this family.
        IVP_U_Point EdgeVector;
        EdgeVector.set(Next->X - Cur->X, Next->Y - Cur->Y, Next->Z - Cur->Z);

        // :0635f52c-:0635f5b4. FUN_0635b940 INLINED: retail recomputes
        // `Next - Cur` a second time on the x87 stack (:0635f561-:0635f574)
        // rather than reusing EdgeVector, and those recomputed differences are
        // UNROUNDED (never stored), unlike EdgeVector's FSTP'd components.
        // Prev - Cur is FSTP'd to float (local_c20/local_c14/local_c10). Then
        // cross(Next - Cur, Prev - Cur) component by component, each product
        // pair subtracted on the stack and FSTP'd once:
        //   x = Ey*Pz - Ez*Py, y = Ez*Px - Ex*Pz, z = Ex*Py - Ey*Px.
        const float PrevX = Prev->X - Cur->X;
        const float PrevY = Prev->Y - Cur->Y;
        const float PrevZ = Prev->Z - Cur->Z;
        const double EdgeXExt = (double)(Next->X) - Cur->X;
        const double EdgeYExt = (double)(Next->Y) - Cur->Y;
        const double EdgeZExt = (double)(Next->Z) - Cur->Z;

        IVP_U_Point FaceNormal;
        FaceNormal.set((float)(EdgeYExt * PrevZ - EdgeZExt * PrevY),
                       (float)(EdgeZExt * PrevX - EdgeXExt * PrevZ),
                       (float)(EdgeXExt * PrevY - EdgeYExt * PrevX));

        // :0635f5bb/:0635f5c4. &EdgeVector is pushed second (:0635f57b) so it
        // is param_1 of the cross (FUN_0628bed0, x87-exact). The result is the
        // in-plane outward normal of this edge: perpendicular to both the edge
        // and the face normal.
        IVP_CalcCrossProduct(&EdgePlane.Normal, &EdgeVector, &FaceNormal);
        EdgePlane.Normal.normize();

        // :0635f5c9-:0635f603. The plane's anchor is this edge's own start
        // vertex, so each of the two iterations tests a different plane.
        EdgePlane.Anchor.set(Cur->X, Cur->Y, Cur->Z);

        // :0635f60a-:0635f626. Threshold is the negated tolerance bias, with
        // nothing multiplied into it; StartSlot 0; null known distance; and
        // here the interval end IS OutEvent*, so pass 1's hit narrows it.
        if (EdgePlane.SearchContactTime(
                -SweepToleranceBias(),
                this->NowBase, this->NowOffset,
                this->OutEventBase, this->OutEventOffset,
                0,
                &CacheA, &CacheB, 0,
                reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
        {
            // :0635f62f.
            this->OutCode = 0x31;
        }
    }

    // ---- Pass 3: the fan of edges at vertex A against B's edge ----------------
    // :0635f6ea rebinds the slot to PTR_LAB_0635d830.
    IVP_Sweep_EdgeFanEdge_Evaluator FanEdge;

    // :0635f650-:0635f67e. How much separation is still unspent at the current
    // end of the interval. DAT_063a545c is raw a6 9b 44 3b == 0.003f, and the
    // clamp's polarity is the mask and the jump together: `FCOMP 0.003f;
    // TEST AH,0x1` (C0 alone) `+ JZ` jumps PAST the clamp when C0 is CLEAR,
    // i.e. when the value is >= 0.003f. The fall-through is strictly less and
    // it is the fall-through that overwrites the value.
    // The whole expression is on the x87 stack (FSUBR at :0635f669); FST
    // dword local_c24 keeps the rounded copy and FCOMP tests the UNROUNDED
    // value, so the clamp also fires on unordered (C0 set).
    const float SeparationGap = LedgeMindist->SeparationGap;
    const double SpanExt =
        (double)(SeparationGap)
        - ((((double)(this->OutEventBase) - this->NowBase) + this->OutEventOffset)
           - this->NowOffset)
              * this->MarginSpan;
    float Span = (float)(SpanExt);
    if (!(SpanExt >= (double)(0.003f)))
    {
        Span = 0.003f; // :0635f67e MOV dword 0x3b449ba6
    }

    // :0635f686-:0635f6f5. The asymmetric speed bound -- see point 4 of the
    // header comment. Retail adds B's term to A's (`FADD ST0,ST1` with A's
    // term in ST1), divides by the (float) span, then adds the plain
    // core-deviation sum from the prologue. TermA and TermB are never stored;
    // one FSTP dword at :0635f714.
    const double TermA = SweepPointLength(VertexA) * CoreA->MaxSurfaceDeviation + CoreA->Margin;
    const double TermB =
        (double)(CoreB->BoundingRadius) * CoreB->MaxSurfaceDeviation + CoreB->Margin;
    const float FanSpeedBound = (float)((TermB + TermA) / Span + SpeedBound);

    // :0635f71a-:0635f742.
    FanEdge.PositionA.set(VertexA->X, VertexA->Y, VertexA->Z);
    FanEdge.PositionB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635f746-:0635f771. B's unit edge direction again -- recomputed from
    // the same two vertices rather than copied from pass 1's evaluator.
    FanEdge.DirectionB.set(FarVertexB->X - VertexB->X,
                           FarVertexB->Y - VertexB->Y,
                           FarVertexB->Z - VertexB->Z);
    FanEdge.DirectionB.normize();

    // :0635f776-:0635f78b.
    FanEdge.MaxRelativeSpeed = FanSpeedBound;
    FanEdge.IMaxRelativeSpeed = 1.0f / FanSpeedBound;

    // :0635f78f-:0635f7b4. `FCOMP` of the gap against a freshly re-fetched
    // ramp entry; mask and jump together: TEST AH,0x1 (C0) + JZ jumps when C0
    // is CLEAR, i.e. when Gap >= Ramp, and the jumped-to path keeps the Ramp
    // that FCOMP left in ST0. The fall-through (Gap strictly less, or
    // unordered) replaces it with the gap. So it is min(Gap, Ramp) with Ramp
    // winning ties -- the OPPOSITE tie-break from item 3369's structurally
    // similar min at :0635dc69, which lets its gap win. Both are transcribed
    // as written.
    const float FanRamp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const float GapOrRamp = (SeparationGap >= FanRamp) ? FanRamp : SeparationGap;

    // :0635f7b4-:0635f7f3. `FADD ST0,ST0` doubles the angular limit factor
    // before the two multiplies, all on the x87 stack; one FSTP dword into
    // local_c18. DAT_063a56f8 is raw 9a 99 99 be == -0.3f, so the threshold is
    // NEGATIVE -- see point 2 of the header comment.
    const float FanThreshold = (float)(
        (((double)(CoreB->DAngularLimitFactor) + CoreB->DAngularLimitFactor)
         * GapOrRamp)
        * (double)(-0.3f));

    // :0635f7c7-:0635f7ec. The same fan-walk expression item 3369 uses, and
    // the same post-tested loop shape: the hub edge is processed on the
    // iteration that closes the fan, not the first.
    const IVP_Compact_Edge *Cursor = EdgeA->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        // :0635f7f9-:0635f843. The far end of this fan edge is the NEXT edge's
        // start point, and the direction is normalized in place.
        const IVP_U_Float_Point *const FarPoint =
            PointsA + Cursor->GetNextEdge()->GetStartPointIndex();

        FanEdge.DirectionA.set(FarPoint->X - VertexA->X,
                               FarPoint->Y - VertexA->Y,
                               FarPoint->Z - VertexA->Z);
        FanEdge.DirectionA.normize();

        // :0635f848-:0635f876. OutEvent* as both interval end and output, so
        // every fan edge searches a window already narrowed by passes 1 and 2
        // and by the fan edges before it.
        if (FanEdge.SearchContactTime(
                FanThreshold,
                this->NowBase, this->NowOffset,
                this->OutEventBase, this->OutEventOffset,
                0,
                &CacheA, &CacheB, 0,
                reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
        {
            // :0635f87f.
            this->OutCode = 0x32;
        }

        // :0635f886-:0635f891, tested after the body.
        if (Cursor == EdgeA)
        {
            break;
        }

        // :0635f893-:0635f8a3.
        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635f020 -- IVP_Sweep_Estimator_Context::EstimateBallVsBallContact
// Common.dll.txt:451231-451353. Bytes 0x0635f020 through the `RET 0x8` at
// 0x0635f177 inclusive -- all 96 instructions read first to last.
//
// Decomp-tracker item 3377, the smallest member of the family and its base
// case: no feature geometry at all, just the two objects' frame origins.
//
// THREE CLAIMS IN THIS ITEM'S OWN PRE-EXISTING TRACKER NOTE ARE STALE, and
// they are why it sat at `simplified` with a blocker since 2026-09-10:
//   * "FUN_0635de90 (item 3370, still `blocked`, unresolved 21-slot candidate-
//     list struct)". Ported since, as IVP_Sweep_Frame_Cache::Init above; the
//     struct is IVP_Sweep_Frame_Cache and its layout is proved.
//   * "FUN_06369720 (item 3598, `partial`/unported) -- the single entry point
//     this function calls into the deliberately-declined-to-port Simplex/LCP
//     solver class". Ported since, as
//     IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact above, and the
//     Simplex/LCP attribution was itself disproved by lane w19-csys-sweep --
//     see this file's own header.
//   * "`Ctx`/`this`'s owning class unidentified ... No struct declared, per
//     'never guess a layout'". It is IVP_Sweep_Estimator_Context, and the
//     offsets this body touches (+0x08, +0x10, +0x18, +0x1c, +0x20, +0x24,
//     +0x28, +0x2c, +0x30) are that struct's exactly.
// The note's actual READING of the code was sound; only its dependencies were
// out of date. Nothing about this function needed new research, just a re-check
// of what it claimed to be waiting on.
//
// WHAT IT IS: a conservative-advancement contact time between two objects
// treated as points at their own frame origins. Evaluate_0635d720 pushes the
// two zeroed local points through vmult4, so the tested positions are exactly
// the two frames' origins; its +0x34 is read RAW and untransformed, and this
// driver fills it with the unit direction between the two origins as they
// stand at the start of the interval. The evaluator's clamp
// (`min(Projected, |Delta|)`, written branchlessly via `fabsf(s)*s`) is what
// keeps the answer honest once the objects rotate away from that direction.
//
// The scalar block here IS the family's shared one -- Threshold = Ramp +
// CombinedRadius, InnerThreshold = CombinedRadius * 0.5f + ToleranceBase
// (DAT_063a5460, raw 00 00 00 3f == 0.5f) -- but the known distance is still a
// literal null (`PUSH 0x0`, :0635f106), as in items 3374 and 3378. Only items
// 3369 and 3371 pass a real one.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsBallContact(
    FIVP_Sweep_Cache_Source *SourceA, FIVP_Sweep_Cache_Source *SourceB)
{
    // :0635f02c-:0635f07e. Two caches, two inline 21-iteration Csys::Csys
    // loops (:0635f038 and :0635f065). This driver spells the
    // vector-constructor-iterator inline where item 3378 calls FUN_06273bc0
    // for it, but Csys::Csys is `MOV EAX,ECX; RET` either way, so both
    // spellings are the same runtime no-op and neither is reproduced.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SourceB);

    // :0635f083-:0635f08c. Seed the out-time with the end of the interval.
    // Note that unlike items 3369, 3374 and 3378, nothing here ever reads
    // OutEvent* back -- the single solver call below is given Next* -- so the
    // seeding exists only so that a miss leaves a usable time behind.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635f09c installs PTR_LAB_0635d720.
    IVP_Sweep_CenterDistance_Evaluator CenterDistance;

    // :0635f0ab-:0635f0e0. Six explicit stores of the integer 0 -- the three
    // components of each point. The 4th float of each (+0x20 and +0x30) is
    // never written and never read, exactly as in item 3371.
    CenterDistance.PointA.set(0.0f, 0.0f, 0.0f);
    CenterDistance.PointB.set(0.0f, 0.0f, 0.0f);

    // :0635f08f-:0635f0ec. `[Source+0x4c]` is Source->Frame.vv -- Frame at
    // +0x1c plus IVP_U_Matrix::vv at +0x30 -- i.e. each object's world-space
    // origin as the caller prepared it. The difference runs B minus A, and
    // retail normalizes it in place and ignores normize's bool return, so a
    // pair of exactly-coincident origins leaves this unnormalized rather than
    // zeroed.
    CenterDistance.Axis.set(SourceB->Frame.vv.k[0] - SourceA->Frame.vv.k[0],
                            SourceB->Frame.vv.k[1] - SourceA->Frame.vv.k[1],
                            SourceB->Frame.vv.k[2] - SourceA->Frame.vv.k[2]);
    CenterDistance.Axis.normize();

    // :0635f0f1-:0635f10f.
    CenterDistance.MaxRelativeSpeed = this->ClosingBound;
    CenterDistance.IMaxRelativeSpeed = 1.0f / CenterDistance.MaxRelativeSpeed;

    // :0635f113-:0635f15d. One x87 stack carries the radius from :0635f113 all
    // the way to the `FADD ST0,ST1` at :0635f156 and is popped unused at
    // :0635f15b -- the same shape item 3371 uses. Ten pushes; from [ESP+0]
    // outward: Threshold, InnerThreshold, NowBase, NowOffset, NextBase,
    // NextOffset, &CacheA, &CacheB, null, &OutEvent.
    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (CenterDistance.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635f166. The lowest event code in the family: 0x10, against 0x20
        // and 0x21 for the ledge-face drivers, 0x30-0x32 for the vertex-edge
        // one and 0x40-0x42 for the edge-edge one.
        this->OutCode = 0x10;
    }
}

// ============================================================================
// FUN_0635e790 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeVertexContact
// Common.dll.txt:450564-450914. Bytes 0x0635e790 through the `RET 0x10` at
// 0x0635ec80 inclusive -- ALL 340 instructions read first to last for this
// pass, x87 stack simulated across every one of the five CALL sequences, and
// every PUSH-induced re-basing of ESP resolved back to frame+N. The frame is
// `SUB ESP,0xbec` plus four pushes, so Ghidra's constant is 0xbfc and
// local_XXX lives at frame+(0xbfc-XXX).
//
// Decomp-tracker item 3375. Its single XREF is :06360060, dispatcher
// LAB_0635fef0's (ShapeTypeA, ShapeTypeB) == (0, 0) arm -- BOTH sides a ledge
// vertex, which is what gives this function its name. That is not inferred
// from the body: the dispatcher's two nested switches are read in full below
// (see IVP_SweepEstimate_LedgeVsLedge) and tag 0 is the arm that also sends
// (0,1) to item 3378 (EstimateLedgeVertexVsLedgeEdge) and (0,2) to item 3369
// (EstimateLedgeVertexVsLedgeFace), so tag 0 on side A is a vertex in three
// independent places.
//
// FRAME TILING, the structural cross-check: evaluator at frame+0x24 (0x44
// bytes, to 0x68), seventeen scalar dwords frame+0x68..0xac, CacheA at
// frame+0xac (0x5a8, to 0x654), CacheB at frame+0x654 (0x5a8, to 0xbfc). End
// to end, no gap, no overlap.
//
// WHAT IT IS: two passes, the second one run twice with the sides exchanged.
//
//   Pass 1 (:0635e88e-:0635e95e) is the vertex-to-vertex distance measured
//   along the mindist's own separating normal, NEGATED. Evaluator
//   PTR_LAB_0635d720 -- the same concrete class item 3377 uses for two ball
//   origins -- with the two real ledge vertices in place of the two zeroes and
//   -SeparatingNormal in place of the centre-to-centre direction. Event code
//   0x10.
//
//   Pass 2 (:0635eaae-:0635ec70) walks the fan of edges around ONE side's
//   feature vertex and asks, for each fan edge, when that edge's direction
//   stops separating it from the OTHER side's vertex. Evaluator
//   PTR_LAB_0635d7b0. Event code 0x11. The outer loop runs exactly twice, once
//   per side, through retail's negative-index/positive-index alias pair.
//
// THE TWO-SIDED ALIAS TRICK, decoded rather than guessed. :0635ea6f/:0635ea7f
// zero two loop variables; :0635ec5f/:0635ec62 step one by -4 and the other by
// +4; :0635ec65 `CMP EAX,-0x8` + `JG` keeps going while the negative one is
// strictly greater than -8, i.e. exactly two iterations with the pair
// (0, 0) then (-4, +4). Every array in the body is a two-element pair of
// adjacent dwords indexed by one or the other:
//     frame+0x94 / +0x98   FeatureEdgeA, FeatureEdgeB   (read at +0x98 + i)
//     frame+0x7c / +0x80   &VertexA, &VertexB           (+0x80 + i and +0x7c + j)
//     frame+0xa4 / +0xa8   SideA, SideB                 (+0xa8 + i)
//     frame+0x6c / +0x70   &CacheA, &CacheB             (+0x70 + i and +0x6c + j)
//     frame+0x9c / +0xa0   the two speed bounds         (+0x9c + j)
// so iteration 0 takes the B side as "self" and iteration 1 takes the A side.
// Written out as a plain two-pass loop here; the arithmetic is recorded
// because it is the only thing that proves which side is which.
//
// THE OUT-TIME IS AGAIN THE INTERVAL END for pass 2 (:0635ebe0/:0635ec04 read
// OutEvent* and :0635ebdc pushes &OutEvent as the destination), so each fan
// edge that scores narrows the window for the rest. Pass 1 is the exception
// and it is retail's, transcribed literally: :0635e932/:0635e91a read
// this->Next* directly rather than this->OutEvent*. At that point the two are
// equal because :0635e822-:0635e83c has just copied one into the other, so the
// difference is unobservable -- but it is written the way retail writes it.
//
// .rdata CONSTANTS, read as RAW BYTES (Ghidra prints a single misleading byte
// for two of these):
//   DAT_063a5460 = 00 00 00 3f  ->  0.5f
//   DAT_063a5498 = 00 00 80 bf  -> -1.0f
//   DAT_063a53d0 = 00 00 80 3f  ->  1.0f
//   DAT_063a56f0 = 00 00 00 bf  -> -0.5f   (NEW this pass; it sits one dword
//                                  below PTR_LAB_0635d720 at 063a56f4 and two
//                                  below the 9a 99 99 be == -0.3f at 063a56f8,
//                                  so reading it a byte at a time is exactly
//                                  how a wrong constant gets into this file)
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeVertexContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635e798-:0635e7cf and :0635e7d4-:0635e80a. Both caches seeded from
    // their side's Source. The two 21-iteration Csys::Csys loops in between are
    // spelled INLINE here (`LEA EDI,...; MOV ECX,EDI; CALL Csys::Csys;
    // ADD EDI,0x40; DEC EBX; JNZ`) where items 3376/3378/3379 call
    // FUN_06273bc0 for the same thing; Csys::Csys is `MOV EAX,ECX; RET` in
    // full, so both spellings are the same runtime no-op.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635e80f-:0635e846. Reached through each side's Object, not through the
    // cache's own Core field the way item 3369 does it.
    const IVP_Core *const CoreA = SideA->Object->core;
    const IVP_Core *const CoreB = SideB->Object->core;

    // :0635e822-:0635e83c.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635e84a-:0635e885. One feature vertex per side, each
    // `(*Edge & 0xffff) * 0x10 + Side->Points`.
    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);

    // ---- Pass 1: the two vertices along the separating normal ---------------
    // :0635e88e installs PTR_LAB_0635d720, i.e. the SAME concrete evaluator
    // class item 3377 builds. Reusing the class is not a convenience: it is the
    // same retail vtable, so it is the same retail type.
    IVP_Sweep_CenterDistance_Evaluator VertexVertex;

    // :0635e864-:0635e8a7. Retail interleaves the two vertex loads, carrying
    // each one's z through a general register while x and y ride the x87 stack.
    VertexVertex.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    VertexVertex.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635e8ab-:0635e8eb. Each component of Mindist->SeparatingNormal
    // (+0x60/+0x64/+0x68) multiplied by DAT_063a5498 == -1.0f. Retail does the
    // three FMULs in the order y, z, x and stores x, y, z; the negation is the
    // whole of it -- there is no normalize here, because SeparatingNormal is
    // already a unit vector by the time the mindist hands it over.
    //
    // Evaluate_0635d720 reads this field RAW and untransformed, so the sign
    // matters and is not absorbed anywhere downstream: it decides which way
    // "closing" points for the projection test.
    VertexVertex.Axis.set(LedgeMindist->SeparatingNormal.x * -1.0f,
                          LedgeMindist->SeparatingNormal.y * -1.0f,
                          LedgeMindist->SeparatingNormal.z * -1.0f);

    // :0635e8ef-:0635e900.
    VertexVertex.MaxRelativeSpeed = this->ClosingBound;
    VertexVertex.IMaxRelativeSpeed = 1.0f / VertexVertex.MaxRelativeSpeed;

    // :0635e904-:0635e94f. The family's shared scalar block, in full this
    // time -- unlike items 3374 and 3378 this driver does read CombinedRadius.
    // One x87 stack carries the radius from :0635e904 to the `FADD ST0,ST1` at
    // :0635e94a and is popped unused at :0635e94f.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // :0635e951. Ten pushes; from [ESP+0] outward: Threshold, InnerThreshold,
    // NowBase, NowOffset, NextBase, NextOffset, &CacheA, &CacheB, null,
    // &OutEvent. The known distance is the literal `PUSH 0x0` at :0635e8a5 --
    // as in items 3374, 3377 and 3378, and unlike items 3369 and 3371.
    if (VertexVertex.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635e95a.
        this->OutCode = 0x10;
    }

    // ---- The two pass-2 speed bounds ---------------------------------------
    // :0635e961-:0635e977. MarginSpan (+0x0c) is exercised here; items 3369,
    // 3371, 3374 and 3377 never touch it, and item 3378 does. Retail's own
    // association order is kept, including that the subtraction of NowOffset
    // happens AFTER the addition of OutEventOffset rather than being folded
    // into a single (End - Now) term.
    // Extended chain, one FSTP dword into local_be8 at :0635e977.
    const float Span = (float)(
        (((((double)(this->OutEventBase) - this->NowBase) + this->OutEventOffset)
          - this->NowOffset)
         * this->MarginSpan)
        + LedgeMindist->SeparationGap);

    // :0635e97b-:0635e99f. FUN_06347e60 is thiscall with the point as `this`;
    // both calls are on the raw feature vertices, i.e. the distance of each
    // vertex from its own object's frame origin. Its result comes back
    // unrounded. TermA is FSTP dword'd into local_bec (:0635e992); TermB is
    // NEVER stored -- it stays in ST1 for both sums below (:0635e9c5,
    // :0635e9ec) and is popped at :0635e9f9.
    const float TermA = (float)(
        SweepPointLength(VertexA) * CoreA->MaxSurfaceDeviation + CoreA->Margin);
    const double TermB =
        SweepPointLength(VertexB) * CoreB->MaxSurfaceDeviation + CoreB->Margin;

    // :0635e9bb-:0635e9f2. TWO bounds, differing only in whose
    // MaxSurfaceDeviation scales the span. The one built with CoreB is the one
    // the B-side pass uses, and vice versa -- proved by the index arithmetic,
    // not by which looks more natural: the loop reads frame+0x9c at j == 0 (the
    // CoreB-weighted value) on the iteration whose "self" side is B.
    //
    // Each is ((Span * MSD) + TermB) + TermA on the x87 stack, one FSTP dword
    // (:0635e9cb into local_b60, :0635e9f2 into local_b5c).
    const float SpeedForSideB = (float)(
        ((double)(Span) * CoreB->MaxSurfaceDeviation + TermB) + TermA);
    const float SpeedForSideA = (float)(
        ((double)(Span) * CoreA->MaxSurfaceDeviation + TermB) + TermA);

    // :0635e9fb-:0635ea12. FCOMPP with ST0 = CoreB->UpperLimitRadius (loaded
    // second, hence on top) and ST1 = CoreA's. `TEST AH,0x1` is C0 alone, and
    // C0 is set on ST0 < ST1 and on unordered; the `JZ` therefore fires when
    // C0 is CLEAR, i.e. when B's radius is >= A's, and reloads B's. So this is
    // max(A, B), with B winning ties (unordered falls through to A's).
    const float LargerRadius = (CoreB->UpperLimitRadius >= CoreA->UpperLimitRadius)
                                   ? CoreB->UpperLimitRadius
                                   : CoreA->UpperLimitRadius;

    // :0635ea12-:0635ea2a. The ramp index is recomputed from StateFlags rather
    // than reusing the one pass 1 already has. Transcribed as retail spells it.
    // `FLD ST0; FMULP` -- the square stays on the x87 stack, never stored.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const double RampSquared = (double)(RampAgain) * RampAgain;

    // :0635ea2c-:0635ea47. Span*Span is computed extended, FST dword'd into
    // local_be0, and the UNROUNDED square is FCOMP'd against RampSquared.
    // `TEST AH,0x1` + `JZ` -- the jump keeps RampSquared and is taken when C0
    // is clear, i.e. when Span*Span >= RampSquared. Otherwise (strictly less
    // or unordered) the ROUNDED square is reloaded from local_be0. So this is
    // min(Span*Span, RampSquared), with RampSquared winning ties.
    const double SpanSquaredExt = (double)(Span) * Span;
    const float SpanSquared = (float)(SpanSquaredExt);
    const double BoundedSquare = (SpanSquaredExt >= RampSquared) ? RampSquared
                                                                 : (double)(SpanSquared);

    // :0635ea47-:0635ea9b. DAT_063a56f0 is -0.5f (raw 00 00 00 bf), and
    // `FDIV ST0,ST1` divides by the larger radius. The result is NEGATIVE, and
    // deliberately so -- SearchContactTime marches while distance > Threshold,
    // so a negative threshold means "keep marching until the fan edge actually
    // crosses".
    //
    // WORTH SEEING: -0.5f / LargerRadius is exactly IVP_Core::
    // DAngularLimitFactor (+0x48, documented in ivp_core.h as
    // 0.5f / UpperLimitRadius) of whichever core has the LARGER radius, with
    // the sign flipped. Item 3376 next door computes the same quantity the
    // other way round -- it has only one ledge side, so it reads that side's
    // own +0x48 directly and negates the product. The two agreeing is the
    // cross-check on this max-versus-min reading. Extended chain, FSTP dword
    // into local_b78 at :0635ea9b.
    const float ThresholdScale =
        (float)((BoundedSquare * (double)(-0.5f)) / LargerRadius);

    // ---- Pass 2, twice: the edge fan around each side's feature vertex ------
    for (int SideIndex = 0; SideIndex < 2; ++SideIndex)
    {
        // :0635eaae-:0635eb51. Iteration 0 is the B side, iteration 1 the A
        // side -- see the alias-trick note in this function's header comment.
        const IVP_Compact_Edge *const EdgeSelf = (SideIndex == 0) ? EdgeB : EdgeA;
        const IVP_U_Float_Point *const VertexSelf = (SideIndex == 0) ? VertexB : VertexA;
        const IVP_U_Float_Point *const VertexOther = (SideIndex == 0) ? VertexA : VertexB;
        const IVP_U_Float_Point *const PointsSelf = (SideIndex == 0) ? PointsB : PointsA;
        IVP_Sweep_Frame_Cache *const CacheSelf = (SideIndex == 0) ? &CacheB : &CacheA;
        IVP_Sweep_Frame_Cache *const CacheOther = (SideIndex == 0) ? &CacheA : &CacheB;
        const float SpeedBound = (SideIndex == 0) ? SpeedForSideB : SpeedForSideA;

        // :0635eac4 rebinds the evaluator slot to PTR_LAB_0635d7b0 on the very
        // same stack bytes pass 1 used. C++ cannot rebind a vptr, so this is a
        // separate local; the check that it is safe is that every field the new
        // evaluator reads is written below before its first use.
        IVP_Sweep_VertexFanEdge_Evaluator FanEdge;

        // :0635eacc-:0635eb01.
        FanEdge.PointOther.set(VertexOther->X, VertexOther->Y, VertexOther->Z);
        FanEdge.PointSelf.set(VertexSelf->X, VertexSelf->Y, VertexSelf->Z);

        // :0635eb07-:0635eb2f.
        FanEdge.MaxRelativeSpeed = SpeedBound;
        FanEdge.IMaxRelativeSpeed = 1.0f / FanEdge.MaxRelativeSpeed;

        // :0635eb1c-:0635eb3b. The same fan seed item 3369 uses: step back one
        // edge inside the triangle, then hop to the twin across that edge.
        const IVP_Compact_Edge *Cursor = EdgeSelf->GetPrevEdge()->GetOppositeEdge();

        for (;;)
        {
            // LAB_0635eb58, :0635eb58-:0635eb98. The far end of this fan edge
            // is the NEXT edge's start vertex, indexed out of the SELF side's
            // already-resolved point array.
            const IVP_U_Float_Point *const FarPoint =
                PointsSelf + Cursor->GetNextEdge()->GetStartPointIndex();

            // :0635eb7b-:0635eb98. Each difference is a single FSUB; x is FSTP
            // dword'd (local_ba4), y and z are stored rounded (local_ba0 via
            // FLD ST1/FSTP, local_b9c via FST) but their UNROUNDED values stay
            // on the x87 stack and are what the squared length squares.
            const float DeltaX = FarPoint->X - VertexSelf->X;
            const double DeltaYExt = (double)(FarPoint->Y) - VertexSelf->Y;
            const double DeltaZExt = (double)(FarPoint->Z) - VertexSelf->Z;
            const float DeltaY = (float)(DeltaYExt);
            const float DeltaZ = (float)(DeltaZExt);

            // :0635eb9c-:0635ebb0, in retail's own association order: the x and
            // z terms are summed first and the y term is added to that. Not the
            // usual (x+y)+z, and float addition is not associative, so the
            // order is kept. x is the ROUNDED delta (FLD/FMUL local_ba4), z and
            // y the unrounded ones (FMUL ST2 / FMUL ST3); FSTP dword local_bdc.
            const float QuadLength = (float)(
                ((double)(DeltaX) * DeltaX + DeltaZExt * DeltaZExt)
                + DeltaYExt * DeltaYExt);

            // :0635ebbd CALL FUN_06347df0 -- FLD/FSQRT/FDIVR DAT_063a53d0 with
            // DAT_063a53d0 == 1.0f, a plain reciprocal square root, returned
            // unrounded.
            const double InverseLength = IVP_InvSqrtFloat(QuadLength);

            // :0635ebc2-:0635ec07. Retail overwrites the three raw components
            // in place with the normalized ones: each ROUNDED delta (local_ba0,
            // local_b9c, local_ba4) times the unrounded InverseLength, FSTP
            // dword.
            FanEdge.Direction.set((float)(DeltaX * InverseLength),
                                  (float)(DeltaY * InverseLength),
                                  (float)(DeltaZ * InverseLength));

            // :0635ec0c-:0635ec15. `InverseLength * QuadLength` is retail's own
            // way of spelling sqrtf(QuadLength) -- it already has the
            // reciprocal on the FPU stack, so it multiplies rather than taking
            // a second square root. Kept literally: the two are not bit-equal.
            // (Inv * Q_float) * ThresholdScale_float on the x87 stack, FSTP
            // dword straight into the argument slot at :0635ec22.
            const float Threshold = (float)((InverseLength * QuadLength) * ThresholdScale);

            // :0635ec25. Note the cache ORDER: the OTHER side's cache is
            // argument seven and this side's is argument eight, which is what
            // makes the evaluator's own +0x14 (PointOther, pushed through
            // FrameA) and +0x24/+0x34 (PointSelf and Direction, pushed through
            // FrameB) land in the right frames. Reversing them would still
            // compile and would silently transform each vertex by the wrong
            // object.
            if (FanEdge.SearchContactTime(
                    Threshold,
                    this->NowBase, this->NowOffset,
                    this->OutEventBase, this->OutEventOffset,
                    0,
                    CacheOther, CacheSelf, 0,
                    reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
            {
                // :0635ec2e.
                this->OutCode = 0x11;
            }

            // :0635ec35-:0635ec39. Tested after the body, so the hub edge is
            // processed on the iteration that closes the fan -- the same shape
            // as item 3369's loop.
            if (Cursor == EdgeSelf)
            {
                break;
            }

            // :0635ec3b-:0635ec4f.
            Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
        }
    }
}

// ============================================================================
// FUN_0635ec90 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeVertexContact
// Common.dll.txt:450963-451226. Bytes 0x0635ec90 through the `RET 0x10` at
// 0x0635f01a inclusive -- ALL 259 instructions read first to last, frame
// `SUB ESP,0xba4` plus four pushes so Ghidra's constant is 0xbb4 and local_XXX
// lives at frame+(0xbb4-XXX).
//
// Decomp-tracker item 3376. Its single XREF is :0635fe07, dispatcher
// LAB_0635fd10's ShapeTypeB == 0 arm.
//
// WHAT IT IS, and how the name is earned. This is item 3375 with side A
// collapsed to a point. Every structural element matches one for one:
//   * pass 1 is the same PTR_LAB_0635d720 evaluator with the same
//     -SeparatingNormal axis and the same (Ramp + CombinedRadius,
//     CombinedRadius*0.5 + ToleranceBase) pair, event code 0x10 -- except that
//     its +0x14 is three literal zeroes (:0635ed50-:0635ed60) instead of a
//     ledge vertex, i.e. object A's own frame origin;
//   * pass 2 is the same PTR_LAB_0635d7b0 fan walk with the same
//     `Cursor = Cursor->GetPrevEdge()->GetOppositeEdge()` rotation and the same
//     event code 0x11 -- except that it runs ONCE, on the B side only, because
//     a point has no fan;
//   * the speed bound is the same expression with A's vertex-length term gone:
//     item 3375 computes `Span*MSD_B + (|VertexB|*MSD_B + Margin_B) +
//     (|VertexA|*MSD_A + Margin_A)` and this one computes `Span*MSD_B +
//     (|VertexB|*MSD_B + Margin_B) + Margin_A` (:0635ee74). |VertexA| is zero
//     for a point at its own origin, so the two agree exactly. That agreement,
//     derived independently from the two listings, is the main cross-check on
//     both readings.
//
// ARGUMENTS. Four, `RET 0x10`, but not item 3375's four:
//   param_1  IVP_Real_Object* -- read ONCE, at :0635ecf0-:0635ecf7, only to
//            reach ->core. Side A has no ledge, no points and no side record,
//            so the dispatcher hands the object itself.
//   param_2  the tagged feature edge on side B.
//   param_3  a FIVP_Sweep_Cache_Source passed DIRECTLY (:0635ecb2 pushes the
//            parameter slot, not something read through it) -- exactly as item
//            3377 takes its two.
//   param_4  the FIVP_Sweep_Ledge_Side for side B.
// Proved at the call site, not by resemblance: LAB_0635fdf7 pushes
// &SideB, SourceA, FeatureB, ObjectA in that order (:0635fdff-:0635fe06), so
// ObjectA is argument one.
//
// THE THRESHOLD SCALE IS NOT ITEM 3375'S, and this is the place a "the
// siblings share their constants" assumption would have produced wrong
// numbers. Item 3375 computes `min(Span^2, Ramp^2) * -0.5f / max(radiusA,
// radiusB)`. This one computes `-(min(Span^2, Ramp^2) * CoreB->
// DAngularLimitFactor)` -- an FMUL by IVP_Core+0x48 followed by an explicit
// `FCHS` (:0635eea1-:0635eeaf), with no division and no max. The two ARE the
// same quantity whenever B is the larger side, because +0x48 is documented as
// 0.5f/UpperLimitRadius, but the expressions are different and only one of them
// is what this function does.
//
// CACHE ORDER. CacheA is built at frame+0x60c from param_3 and CacheB at
// frame+0x64 from param_4->Source -- i.e. the HIGHER address is the A side
// here, the opposite of item 3375's arrangement. Both solver calls push
// frame+0x60c as argument seven (:0635ede9, :0635efa7), so the A cache really
// is the evaluator's FrameA, matching its zeroed +0x14. Frame tiling confirms
// the two extents: CacheB 0x64..0x60c, CacheA 0x60c..0xbb4.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeVertexContact(
    IVP_Real_Object *ObjectA, const void *FeatureEdgeB,
    FIVP_Sweep_Cache_Source *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635ec9a-:0635ecc1 and :0635ecd0-:0635eceb. Both vector-constructor-
    // iterator calls (FUN_06273bc0 over 21 * 0x40 bytes with Csys::Csys) are
    // the usual runtime no-op and are not reproduced.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635ecf0-:0635ed1c.
    const IVP_Core *const CoreA = ObjectA->core;
    const IVP_Core *const CoreB = SideB->Object->core;

    // :0635ed06-:0635ed13.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635ed16-:0635ed38.
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);

    // ---- Pass 1: A's origin against B's feature vertex ----------------------
    // :0635ed41 installs PTR_LAB_0635d720.
    IVP_Sweep_CenterDistance_Evaluator OriginVertex;

    // :0635ed50-:0635ed60 write three literal zeroes; :0635ed35-:0635ed68 write
    // the vertex. The 4th float of each point is never written and never read.
    OriginVertex.PointA.set(0.0f, 0.0f, 0.0f);
    OriginVertex.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635ed6c-:0635ed98, same negation as item 3375.
    OriginVertex.Axis.set(LedgeMindist->SeparatingNormal.x * -1.0f,
                          LedgeMindist->SeparatingNormal.y * -1.0f,
                          LedgeMindist->SeparatingNormal.z * -1.0f);

    // :0635ed9c-:0635edad.
    OriginVertex.MaxRelativeSpeed = this->ClosingBound;
    OriginVertex.IMaxRelativeSpeed = 1.0f / OriginVertex.MaxRelativeSpeed;

    // :0635edb1-:0635ee02. The shared scalar block; known distance is the
    // literal `PUSH 0x0` at :0635edd2.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (OriginVertex.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635ee0d.
        this->OutCode = 0x10;
    }

    // ---- The pass-2 speed bound --------------------------------------------
    // :0635ee14-:0635ee38. Identical expression to item 3375's Span: extended
    // chain, one FSTP dword into local_ba4.
    const float Span = (float)(
        (((((double)(this->OutEventBase) - this->NowBase) + this->OutEventOffset)
          - this->NowOffset)
         * this->MarginSpan)
        + LedgeMindist->SeparationGap);

    // :0635ee3c-:0635ee74. CoreA contributes only its Margin -- there is no
    // vertex on side A to measure. Retail caches CoreA->Margin into the slot it
    // had been keeping CoreA in (:0635ee20-:0635ee2b) before the call that
    // clobbers the register, which is why the read happens so early.
    // (|VB| * MSD_B + Margin_B) + Span * MSD_B via FADDP, then + Margin_A --
    // the whole bound stays on the x87 stack (never rounded until :0635eef3).
    const double SpeedBoundExt =
        ((SweepPointLength(VertexB) * CoreB->MaxSurfaceDeviation + CoreB->Margin)
         + (double)(Span) * CoreB->MaxSurfaceDeviation)
        + CoreA->Margin;

    // :0635ee78-:0635eeaf. See this function's header comment: the scale is
    // CoreB's own DAngularLimitFactor, negated, with no max and no division.
    // Ramp^2 stays extended (FLD ST0; FMULP); Span^2 is extended, FST dword'd
    // into local_ba0 and FCOMP'd unrounded; TEST AH,0x1 + JZ keeps Ramp^2 when
    // Span^2 >= Ramp^2, otherwise (less or unordered) reloads the ROUNDED
    // Span^2. FMUL ST1 (DALF) / FCHS / FSTP dword local_b9c.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const double RampSquared = (double)(RampAgain) * RampAgain;
    const double SpanSquaredExt = (double)(Span) * Span;
    const float SpanSquared = (float)(SpanSquaredExt);
    const double BoundedSquare = (SpanSquaredExt >= RampSquared) ? RampSquared
                                                                 : (double)(SpanSquared);
    const float ThresholdScale =
        (float)(-(BoundedSquare * CoreB->DAngularLimitFactor));

    // ---- Pass 2: the edge fan around B's feature vertex ---------------------
    // :0635eebd rebinds the evaluator slot to PTR_LAB_0635d7b0 on the same
    // stack bytes; a separate local here, for the same reason as item 3375.
    IVP_Sweep_VertexFanEdge_Evaluator FanEdge;

    // :0635eec7-:0635eeef. PointOther is again the literal origin -- retail
    // re-zeroes the three dwords rather than relying on pass 1's, and so does
    // this port.
    FanEdge.PointOther.set(0.0f, 0.0f, 0.0f);
    FanEdge.PointSelf.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635eef3-:0635eeff. FST dword stores the rounded bound into +0x04, but
    // `FLD 1.0; FDIV ST0,ST1` divides by the UNROUNDED bound still in ST1 --
    // unlike every other driver, which reloads the stored float.
    FanEdge.MaxRelativeSpeed = (float)(SpeedBoundExt);
    FanEdge.IMaxRelativeSpeed = (float)(1.0 / SpeedBoundExt);

    // :0635eeac-:0635ef0f.
    const IVP_Compact_Edge *Cursor = EdgeB->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        // LAB_0635ef13, :0635ef13-:0635ef4f.
        const IVP_U_Float_Point *const FarPoint =
            PointsB + Cursor->GetNextEdge()->GetStartPointIndex();

        // :0635ef32-:0635ef4f. Same stack shape as item 3375: x FSTP dword,
        // y and z stored rounded but kept UNROUNDED on the x87 stack.
        const float DeltaX = FarPoint->X - VertexB->X;
        const double DeltaYExt = (double)(FarPoint->Y) - VertexB->Y;
        const double DeltaZExt = (double)(FarPoint->Z) - VertexB->Z;
        const float DeltaY = (float)(DeltaYExt);
        const float DeltaZ = (float)(DeltaZExt);

        // :0635ef53-:0635ef67, the same (x + z) + y association as item 3375:
        // rounded x, unrounded z and y, FSTP dword local_b98.
        const float QuadLength = (float)(
            ((double)(DeltaX) * DeltaX + DeltaZExt * DeltaZExt)
            + DeltaYExt * DeltaYExt);

        // :0635ef74 CALL FUN_06347df0, result unrounded.
        const double InverseLength = IVP_InvSqrtFloat(QuadLength);

        // :0635ef79-:0635efb8. Rounded deltas times the unrounded inverse.
        FanEdge.Direction.set((float)(DeltaX * InverseLength),
                              (float)(DeltaY * InverseLength),
                              (float)(DeltaZ * InverseLength));

        // :0635efbd-:0635efd0. (Inv * Q_float) * Scale_float, FSTP dword.
        const float Threshold = (float)((InverseLength * QuadLength) * ThresholdScale);

        // :0635efd3. CacheA is argument seven, CacheB argument eight -- the
        // straightforward order here, because there is only one ledge side and
        // it is B.
        if (FanEdge.SearchContactTime(
                Threshold,
                this->NowBase, this->NowOffset,
                this->OutEventBase, this->OutEventOffset,
                0,
                &CacheA, &CacheB, 0,
                reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
        {
            // :0635efdc.
            this->OutCode = 0x11;
        }

        // :0635efe3-:0635efee. Again tested after the body.
        if (Cursor == EdgeB)
        {
            break;
        }

        // :0635eff0-:0635f009.
        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635f8c0 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeEdgeContact
// Common.dll.txt:451980-452276. Bytes 0x0635f8c0 through the `RET 0x10` at
// 0x0635fd04 inclusive -- ALL 293 instructions read first to last, frame
// `SUB ESP,0xc08` plus four pushes so Ghidra's constant is 0xc18.
//
// Decomp-tracker item 3379. Its single XREF is :0635fdf0, dispatcher
// LAB_0635fd10's ShapeTypeB == 1 arm -- a ledge EDGE, which names it.
//
// FRAME TILING: evaluator at frame+0x60 (0x58 bytes, to 0xb8), a 0x10-byte
// cross-product scratch at frame+0xb8, CacheB at frame+0xc8 (0x5a8, to 0x670),
// CacheA at frame+0x670 (0x5a8, to 0xc18). End to end from 0xb8 onwards.
//
// WHAT IT IS: two passes over one ledge edge, with a ball on side A.
//
//   Pass 1 (:0635f942-:0635fb08) is the MOMENT test. Evaluator
//   PTR_LAB_0635d950 -- the odd one of the nine, whose +0x14 is a scalar bias
//   and whose points start at +0x18. Its four fields are A's origin, B's edge
//   start vertex, B's unit edge direction, and a "measure" direction built in
//   world space and carried back into B's frame. Event code 0x30.
//
//   Pass 2 (:0635fb39-:0635fcd9) runs TWICE, over the edge and its twin, and
//   asks when the ball crosses each triangle's own side plane through that
//   edge. Evaluator PTR_LAB_0635d5b0 (the same class item 3371 uses), with the
//   plane anchored at the edge's start vertex and its normal built as
//   `(PNext - PCur) x FaceNormal`. Event code 0x31.
//
// THIS DRIVER READS NO IVP_Core AT ALL. Not MaxSurfaceDeviation, not Margin,
// not UpperLimitRadius, not DAngularLimitFactor -- there is no `[reg+0x98]`
// anywhere in its 293 instructions. Its two evaluators take their speed bounds
// straight from the context: Ctx->ClosingBound (+0x08) for pass 1 (:0635f9df)
// and Ctx->MarginSpan (+0x0c) for pass 2 (:0635fb0f). A transcription that
// copied item 3375's or 3376's core-weighted bound here would be wrong twice
// over.
//
// ITS INNER THRESHOLD IS `CombinedRadius * 0.9f + ToleranceBase`, not
// `* 0.5f` -- DAT_063a53fc at :0635faca, raw bytes 66 66 66 3f == 0.9f, where
// every other member of the family multiplies by DAT_063a5460 (00 00 00 3f ==
// 0.5f). Item 3378 is the only other driver that does this. Ghidra's inline
// comment on DAT_063a53fc prints "= 66h", one byte, which is meaningless.
//
// ITS PASS-2 THRESHOLD IS A BARE NEGATED GLOBAL, not a computed distance:
// `-DAT_0658ab34` (:0635fcb1-:0635fcbb), i.e. -block[0x49] of the tolerance
// object, the same SweepToleranceBias() item 3369 uses inside a product. No
// span, no radius, no per-edge length. That is the third place in this one
// function where the family pattern does not hold.
//
// param_1 IS UNUSED. Ghidra could not assign it a stack slot and the body
// references only Stack[0x8]/Stack[0xc]/Stack[0x10]; the signature keeps it
// because the dispatcher pushes the same four arguments for this arm as for
// item 3376's.
//
// THE THREE TRANSFORM HELPERS, and the trap lane w23 flagged. Retail calls
// FUN_0634d8d0 (:0635fa2f), FUN_0634d9d0 (:0635fa44) and FUN_0634d970
// (:0635fa93), each with `ECX = SideB->Source`. All three are already ported in
// this tree, in ivp_real_object.h, as IVP_TransformSlotToWorldSpace (vmult4),
// IVP_RotateSlotToWorldSpace (vmult) and IVP_RotateWorldToSlotSpace (vimult),
// and all three read their matrix at `this + 0x1c` -- so the receiver is the
// cache source and NOT a matrix. Handing them a matrix reads 0x1c bytes early.
// This file spells them `Source->Frame.<op>()`, which is the same three
// operations on the same three matrices with one indirection fewer; the
// equivalence is exact because FIVP_Sweep_Cache_Source::Frame is at +0x1c.
// (:0635fa93 appears verbatim in FUN_0634d970's own XREF list in
// ivp_real_object.h, which is the independent confirmation that these are the
// same functions -- and, with the +0x08/+0x1c field match, that
// FIVP_Sweep_Cache_Source is IVP_NarrowPhaseCacheSlot. See the header.)
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeEdgeContact(
    IVP_Real_Object * /*ObjectA*/, const void *FeatureEdgeB,
    FIVP_Sweep_Cache_Source *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    // :0635f8ca-:0635f8f1 and :0635f900-:0635f91e. CacheA from param_3
    // directly (the ball side), CacheB from param_4->Source.
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // :0635f923-:0635f933.
    this->OutEventBase = this->NextBase;
    this->OutEventOffset = this->NextOffset;

    // :0635f929-:0635f988. Both ends of the feature edge, out of side B's
    // already-resolved point array.
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexBNext =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist =
        static_cast<const IVP_Ledge_Mindist *>(this->Mindist);

    // ---- Pass 1: the moment test -------------------------------------------
    // :0635f942 installs PTR_LAB_0635d950.
    IVP_Sweep_VertexEdgeMoment_Evaluator EdgeMoment;

    // :0635f95f-:0635f96f. A's own frame origin.
    EdgeMoment.PointA.set(0.0f, 0.0f, 0.0f);

    // :0635f97a-:0635f99f.
    EdgeMoment.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // :0635f9a6-:0635f9da. The edge direction in B's own local frame,
    // normalized in place by FUN_06347ef0. Retail ignores normize's bool
    // return, so a degenerate edge is left unnormalized rather than zeroed.
    EdgeMoment.DirectionB.set(VertexBNext->X - VertexB->X,
                              VertexBNext->Y - VertexB->Y,
                              VertexBNext->Z - VertexB->Z);
    EdgeMoment.DirectionB.normize();

    // :0635f9df-:0635f9f3.
    EdgeMoment.MaxRelativeSpeed = this->ClosingBound;
    EdgeMoment.IMaxRelativeSpeed = 1.0f / EdgeMoment.MaxRelativeSpeed;

    // :0635f9f7-:0635fa2b. The scalar bias at the evaluator's +0x14, which
    // Evaluate_0635d950 adds to its moment dot product before the min against
    // the moment's own length.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    EdgeMoment.Bias = (Ramp + CombinedRadius) * 0.5f;

    // :0635fa2f. FUN_0634d8d0 on SideB->Source: B's feature vertex carried into
    // WORLD space, translation included.
    IVP_U_Point WorldVertexB;
    SideB->Source->Frame.vmult4(&EdgeMoment.PointB, &WorldVertexB);

    // :0635fa44. FUN_0634d9d0 on the same receiver: the edge direction rotated
    // into world space, no translation.
    IVP_U_Point WorldDirectionB;
    SideB->Source->Frame.vmult(&EdgeMoment.DirectionB, &WorldDirectionB);

    // :0635fa49-:0635fa77. `[EBP+0x4c/+0x50/+0x54]` is SourceA->Frame.vv --
    // Frame at +0x1c plus IVP_U_Matrix::vv at +0x30 -- i.e. the ball's own
    // world-space origin. The difference runs (vertex - ball).
    IVP_U_Point WorldDelta;
    WorldDelta.set(WorldVertexB.k[0] - SourceA->Frame.vv.k[0],
                   WorldVertexB.k[1] - SourceA->Frame.vv.k[1],
                   WorldVertexB.k[2] - SourceA->Frame.vv.k[2]);

    // :0635fa7b CALL FUN_0628bed0, whose receiver is the OUTPUT and whose
    // param_1 is the argument sitting at [ESP+0]. Retail pushes
    // &WorldDirectionB first and &WorldDelta second, so &WorldDelta is
    // param_1 and the product is cross(WorldDelta, WorldDirectionB), not the
    // reverse. Argument order taken from the push order, not assumed. The
    // products stay on the x87 stack, one float rounding per component
    // (IVP_CalcCrossProduct).
    IVP_U_Point WorldMoment;
    IVP_CalcCrossProduct(&WorldMoment, &WorldDelta, &WorldDirectionB);

    // :0635fa93 FUN_0634d970 (the transpose rotate, world -> B-local) then
    // :0635fa9f normize in place. This is the evaluator's +0x48.
    SideB->Source->Frame.vimult(&WorldMoment, &EdgeMoment.MeasureB);
    EdgeMoment.MeasureB.normize();

    // :0635faa4-:0635faff. Threshold = Ramp + CombinedRadius as usual, but the
    // inner threshold scales the radius by 0.9f, not 0.5f -- see this
    // function's header comment. Known distance is the literal `PUSH 0x0` at
    // :0635faab. Note the ramp index is recomputed from StateFlags here rather
    // than reusing the value :0635fa0a already loaded.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (EdgeMoment.SearchContactTimeFromContact(
            RampAgain + CombinedRadius,
            CombinedRadius * 0.9f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
    {
        // :0635fb08. A different code family from items 3375/3376/3377's 0x10.
        this->OutCode = 0x30;
    }

    // ---- Pass 2: the two side planes through this edge ----------------------
    // :0635fb0f-:0635fb69. Retail builds a two-element array {FeatureEdgeB,
    // FeatureEdgeB->GetOppositeEdge()} on its own frame at frame+0x40 and walks
    // it with a pointer and a down-counter initialized to 2
    // (:0635fb31/:0635fb59/:0635fb5d/:0635fb61, loop closed at :0635fce0-
    // :0635fcf4). The evaluator's vfptr is rebound to PTR_LAB_0635d5b0 and its
    // speed bound comes from Ctx->MarginSpan, NOT from Ctx->ClosingBound the
    // way pass 1's does.
    const IVP_Compact_Edge *const EdgePair[2] = { EdgeB, EdgeB->GetOppositeEdge() };

    for (int PairIndex = 0; PairIndex < 2; ++PairIndex)
    {
        const IVP_Compact_Edge *const Edge = EdgePair[PairIndex];

        IVP_Sweep_PointPlane_Evaluator SidePlane;

        // :0635fb41-:0635fb51. A's origin again.
        SidePlane.PointA.set(0.0f, 0.0f, 0.0f);

        // :0635fb0f-:0635fb69, hoisted out of the loop by retail but written
        // into the same slot each time the evaluator is rebound, so it is
        // written here too.
        SidePlane.MaxRelativeSpeed = this->MarginSpan;
        SidePlane.IMaxRelativeSpeed = 1.0f / SidePlane.MaxRelativeSpeed;

        // LAB_0635fb76, :0635fb76-:0635fbd9. The triangle's three vertices,
        // fetched through the next/prev step tables off THIS edge.
        const IVP_U_Float_Point *const PointCur = PointsB + Edge->GetStartPointIndex();
        const IVP_U_Float_Point *const PointNext =
            PointsB + Edge->GetNextEdge()->GetStartPointIndex();
        const IVP_U_Float_Point *const PointPrev =
            PointsB + Edge->GetPrevEdge()->GetStartPointIndex();

        const float EdgeX = PointNext->X - PointCur->X;
        const float EdgeY = PointNext->Y - PointCur->Y;
        const float EdgeZ = PointNext->Z - PointCur->Z;

        // :0635fbdd-:0635fc5c. The face normal (FUN_0635b940 inlined), spelled
        // out component by component exactly as retail does it: with
        // P = Prev - Cur (FSTP'd to float, local_c00/local_bf4/local_bfc)
        // and E = Next - Cur RECOMPUTED on the x87 stack and never stored
        // (:0635fc0f-:0635fc1f -- not the FSTP'd EdgeX/Y/Z above), the
        // result is E x P, each component one FSTP dword:
        //   x = Ey*Pz - Ez*Py, y = Ez*Px - Ex*Pz, z = Ex*Py - Ey*Px.
        // Written inline because retail indexes SideB->Points (the resolved
        // array) and not the ledge.
        const float PrevX = PointPrev->X - PointCur->X;
        const float PrevY = PointPrev->Y - PointCur->Y;
        const float PrevZ = PointPrev->Z - PointCur->Z;
        const double EdgeXExt = (double)(PointNext->X) - PointCur->X;
        const double EdgeYExt = (double)(PointNext->Y) - PointCur->Y;
        const double EdgeZExt = (double)(PointNext->Z) - PointCur->Z;

        IVP_U_Point FaceNormal;
        FaceNormal.set((float)(EdgeYExt * PrevZ - EdgeZExt * PrevY),
                       (float)(EdgeZExt * PrevX - EdgeXExt * PrevZ),
                       (float)(EdgeXExt * PrevY - EdgeYExt * PrevX));

        IVP_U_Point EdgeDirection;
        EdgeDirection.set(EdgeX, EdgeY, EdgeZ);

        // :0635fc60 CALL FUN_0628bed0 with &EdgeDirection at [ESP+0] and
        // &FaceNormal at [ESP+4], so param_1 is the edge direction:
        // cross(EdgeDirection, FaceNormal), x87-exact (IVP_CalcCrossProduct).
        // :0635fc6c normalizes in place. The result is the in-plane normal of
        // the edge's own side plane.
        IVP_CalcCrossProduct(&SidePlane.Normal, &EdgeDirection, &FaceNormal);
        SidePlane.Normal.normize();

        // :0635fc71-:0635fcaa. The plane's anchor is this edge's start vertex,
        // in B's local frame. Evaluate_0635d5b0 pushes it through FrameB with
        // vmult4 and the normal through FrameB with vmult.
        SidePlane.Anchor.set(PointCur->X, PointCur->Y, PointCur->Z);

        // :0635fcb1-:0635fcd0. The threshold is a bare negated global, and the
        // end of the interval comes from OutEvent* (:0635fc8a/:0635fca7) so
        // pass 1's hit narrows this test's window. Known distance is the
        // literal `PUSH 0x0` at :0635fc7e; start slot is the `PUSH 0x0` at
        // :0635fc97.
        if (SidePlane.SearchContactTime(
                -SweepToleranceBias(),
                this->NowBase, this->NowOffset,
                this->OutEventBase, this->OutEventOffset,
                0,
                &CacheA, &CacheB, 0,
                reinterpret_cast<IVP_Time *>(&this->OutEventBase)) != 0)
        {
            // :0635fcd9.
            this->OutCode = 0x31;
        }
    }
}

// ============================================================================
// THE DISPATCH LAYER  (2026-09-17, lane claude:w24-sweep-rest)
//
// Four handlers and a table, sitting between IVP_RescheduleCollisionEvent
// (item 3117) and the eight drivers above. See the header for the table's own
// layout and for why these are __cdecl rather than thiscalls.
//
// ONE THING WORTH SAYING BEFORE THE CODE: the two big handlers are what NAME
// the drivers. Until this pass the eight drivers' names came from reading their
// bodies and inferring which feature pairing each one was for. The dispatchers
// settle it from outside, because each one switches on IVP_Ledge_Mindist's own
// ShapeType tags and every arm is a different driver:
//     LAB_0635fd10, on ShapeTypeB alone (side A is a ball):
//         0 -> FUN_0635ec90   ball vs ledge VERTEX   (item 3376)
//         1 -> FUN_0635f8c0   ball vs ledge EDGE     (item 3379)
//         2 -> FUN_0635df00   ball vs ledge FACE     (item 3371)
//     LAB_0635fef0, on both tags:
//         (0,0) -> FUN_0635e790   vertex vs vertex   (item 3375)
//         (0,1) -> FUN_0635f180   vertex vs edge     (item 3378)
//         (0,2) -> FUN_0635da70   vertex vs face     (item 3369)
//         (1,1) -> FUN_0635e1d0   edge   vs edge     (item 3374)
// So tag 0 == vertex, 1 == edge, 2 == face, 3 == ball, consistently in seven
// places. Every one of the five driver names lane w22/w23 chose from the bodies
// alone is confirmed, and the three named this pass are named from here rather
// than from a body reading. Note also that (1,0), (1,2) and (2,*) are retail's
// own null-store traps -- the mindist normalizes the pairing so that the
// lower-dimensional feature is always on side A.
// ============================================================================

namespace
{
    // The 0x1c-byte "side slot" IVP_Ledge_Mindist embeds twice, at +0x18 and
    // +0x34. It is not declared in ivp_mindist.h -- that header spells the two
    // copies out as flat fields (ObjectA/FeatureA/ShapeTypeA at +0x28/+0x2c/
    // +0x32 and ObjectB/FeatureB/ShapeTypeB at +0x44/+0x48/+0x4e) -- but
    // LAB_0635fef0 indexes them as an array and there is no honest way to
    // transcribe that without the stride.
    //
    // The stride is retail's own: `EDX = i*8 - i` (i.e. i*7) then
    // `[Mindist + EDX*4 + 0x28]`, which is `Mindist + 0x28 + i*0x1c`. Subtract
    // the 0x10 that lands on Object and the slot base is +0x18 + i*0x1c. Both
    // fixed spellings fall out: i == 0 gives +0x28/+0x2c/+0x32, i == 1 gives
    // +0x44/+0x48/+0x4e. The flat names in ivp_mindist.h and this stride are
    // the same bytes; this struct is a view, not a second declaration.
    struct FIVP_Sweep_Mindist_Side
    {
        int32_t Field_0x00;              // +0x00 -- SynapseLinkDelta word + pad
        int32_t Field_0x04;              // +0x04 -- SidePool handle
        int32_t Field_0x08;              // +0x08 -- unread here
        int32_t Field_0x0c;              // +0x0c -- unread here
        IVP_Real_Object *Object;         // +0x10
        IVP_Compact_Edge *Feature;       // +0x14 -- tagged pointer
        int16_t Field_0x18;              // +0x18 -- unread here
        int16_t ShapeType;               // +0x1a
    };
    static_assert(sizeof(FIVP_Sweep_Mindist_Side) == 0x1c,
                  "mindist side slot must be retail's 0x1c-byte stride");

    FIVP_Sweep_Mindist_Side *SweepMindistSide(IVP_Ledge_Mindist *Mindist, uint32_t Index)
    {
        return reinterpret_cast<FIVP_Sweep_Mindist_Side *>(
            reinterpret_cast<char *>(Mindist) + 0x18 + Index * 0x1c);
    }

    // Retail's own "this cannot happen" store, `MOV dword ptr [0x00000000],0x0`.
    // Reproduced as a null dereference so it stays exactly as loud as retail
    // made it -- this is an assert, not an error path, and quietly returning
    // would turn a detectable bug into a silently wrong simulation.
    void SweepEstimatorUnreachable()
    {
        *reinterpret_cast<volatile int32_t *>(0) = 0;
    }

    // The 16-slot table FUN_06360090 fills, plus retail's own 17th dword --
    // ivp_environment.cpp's existing port already sizes it at 17 and explains
    // why; kept identical here so the two agree.
    FIVP_Sweep_Estimator_Handler G_SweepEstimatorTable[16] = {};
}

// ============================================================================
// LAB_0635fd10 -- IVP_SweepEstimate_BallVsLedge  (decomp-tracker item 3380)
// Common.dll.txt:452287-452389. Bytes 0x0635fd10 through the `RET` at
// 0x0635fe25 inclusive -- all 97 instructions read first to last.
//
// __cdecl, one stack argument, plain `RET`. Side A is a ball, side B is a
// ledge, and the arm is chosen by Mindist->ShapeTypeB alone.
// ============================================================================
void IVP_SweepEstimate_BallVsLedge(IVP_Sweep_Estimator_Context *Ctx)
{
    // :0635fd13-:0635fd26.
    IVP_Ledge_Mindist *const Mindist = static_cast<IVP_Ledge_Mindist *>(Ctx->Mindist);
    IVP_Real_Object *const ObjectA = Mindist->synapse[0].l_obj;
    IVP_Real_Object *const ObjectB = Mindist->synapse[1].l_obj;
    IVP_Compact_Edge *const FeatureB = Mindist->synapse[1].edge;

    // :0635fd20-:0635fd67. Retail INLINES IVP_Real_Object::
    // ResolveNarrowPhaseCacheSlot here (allocate into Object->[0x40] when null
    // via FUN_0634d530, ++Slot->RefCount, then refresh via FUN_0634d640 when
    // the object is moving and the environment's tick counter is newer than the
    // slot's stamp) and CALLS it for the other side at :0635fd8f. Same
    // function, two spellings; this port uses the method for both.
    FIVP_Sweep_Cache_Source *const SourceA =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectA->ResolveNarrowPhaseCacheSlot());

    // :0635fd74-:0635fd87. The containing ledge, recovered from the tagged
    // feature edge -- `Edge & ~0xf`, then back off (tri_index + 1) triangles.
    // That is IVP_Compact_Edge::GetCompactLedge() term for term.
    IVP_Compact_Ledge *const LedgeB = FeatureB->GetCompactLedge();

    // :0635fd89-:0635fda4. The 0x10-byte side record, built on the handler's
    // own frame. Note Points is `Ledge + Ledge->CPointOffset`, i.e. already
    // resolved to an absolute pointer -- which is why the drivers index it with
    // a bare `index*0x10 + Points`.
    FIVP_Sweep_Ledge_Side SideB;
    SideB.Points = LedgeB->GetPointArray();
    SideB.Ledge = LedgeB;
    SideB.Source =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectB->ResolveNarrowPhaseCacheSlot());
    SideB.Object = ObjectB;

    // :0635fda8. Cleared before dispatch, which is why every driver only ever
    // writes the nonzero code.
    Ctx->OutCode = 0;

    // :0635fdaf-:0635fe07. Retail's own three-way switch, `MOVSX EAX,word ptr
    // [EBX+0x4e]` then a chain of SUB/DEC + JZ. All three arms receive the
    // UNMASKED feature edge saved at :0635fd29 -- the `AND EAX,0xfffffff0` at
    // :0635fd76 is a separate temporary used only to find the ledge.
    switch (Mindist->synapse[1].status)
    {
    case 0:
        // :0635fdf7-:0635fe07.
        Ctx->EstimateBallVsLedgeVertexContact(ObjectA, FeatureB, SourceA, &SideB);
        break;
    case 1:
        // :0635fde0-:0635fdf0.
        Ctx->EstimateBallVsLedgeEdgeContact(ObjectA, FeatureB, SourceA, &SideB);
        break;
    case 2:
        // :0635fdca-:0635fdd9. Three arguments, not four -- this driver is the
        // only one of the three that does not take the object.
        Ctx->EstimateBallVsLedgeFaceContact(FeatureB, SourceA, &SideB);
        break;
    default:
        // :0635fdbe, retail's null store.
        SweepEstimatorUnreachable();
        break;
    }

    // :0635fe0c-:0635fe1e. Both refcounts released, ObjectA's first.
    ObjectA->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SourceA));
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SideB.Source));
}

// ============================================================================
// FUN_0635fe30 -- IVP_SweepEstimate_BallVsBall  (decomp-tracker item 3381)
// Common.dll.txt:452406-452469. Bytes 0x0635fe30 through the `RET` at
// 0x0635fee3 inclusive -- all 60 instructions read first to last.
//
// The simplest of the three real handlers: no ledge, no feature edge, no shape
// switch. Both sides resolve their cache slot and the pair goes straight to
// item 3377.
//
// THIS ITEM'S RECORDED BLOCKER IS STALE. It reads "NOT portable yet:
// FUN_0635f020 (item 3377) is itself only `simplified` with NO ported_file
// committed ... it calls into the deliberately-unported LCP/Simplex solver
// subsystem (items 3540-3598) via FUN_06369720/item 3598, and further depends
// on item 3370". Item 3377 is `ready_unverified` and ported as
// EstimateBallVsBallContact above; item 3370 is ported as
// IVP_Sweep_Frame_Cache::Init; item 3598 is ported as
// SearchContactTimeFromContact; and the LCP attribution was refuted outright by
// lane w19-lcp-audit. The blocker's own closing sentence -- "once
// 3377/3370/3598's chain closes, this function ports cleanly" -- is exactly
// what happened.
// ============================================================================
void IVP_SweepEstimate_BallVsBall(IVP_Sweep_Estimator_Context *Ctx)
{
    // :0635fe31-:0635fe3d.
    IVP_Ledge_Mindist *const Mindist = static_cast<IVP_Ledge_Mindist *>(Ctx->Mindist);
    IVP_Real_Object *const ObjectA = Mindist->synapse[0].l_obj;
    IVP_Real_Object *const ObjectB = Mindist->synapse[1].l_obj;

    // :0635fe40-:0635fe87 and :0635fe82-:0635fec7. Two inlined copies of
    // IVP_Real_Object::ResolveNarrowPhaseCacheSlot, one per side, byte for byte
    // the shape of that method's own body.
    FIVP_Sweep_Cache_Source *const SourceA =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectA->ResolveNarrowPhaseCacheSlot());
    FIVP_Sweep_Cache_Source *const SourceB =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectB->ResolveNarrowPhaseCacheSlot());

    // :0635fece.
    Ctx->OutCode = 0;

    // :0635fed5.
    Ctx->EstimateBallVsBallContact(SourceA, SourceB);

    // :0635feda-:0635fedd, an inline `DEC [Slot+4]` per side rather than a call.
    ObjectA->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SourceA));
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SourceB));
}

// ============================================================================
// LAB_0635fef0 -- IVP_SweepEstimate_LedgeVsLedge  (decomp-tracker item 3382)
// Common.dll.txt:452483-452621. Bytes 0x0635fef0 through the `RET` at
// 0x0636007e inclusive -- all 131 instructions read first to last.
//
// Both sides are ledges, so both sides need a side record, and which of the
// mindist's two embedded slots is "A" is chosen at run time by StateFlags.
//
// THE SIDE SELECTION, decoded from the arithmetic rather than assumed:
//     EAX = Mindist->StateFlags;  i = (EAX >> 8) & 3;
//     EAX ^= 0x100;               j = (EAX >> 8) & 3;
// The XOR flips BIT 8 ONLY, so j is always i with bit 0 toggled -- the two are
// a pair, never two independent selections. Item 3117's own trace reached the
// same conclusion from the same instructions. The `& 3` is the compiler masking
// a two-bit field of which only one bit is ever meaningful, and it is kept here
// (rather than simplified to `& 1`) because it is what retail computes: the
// stride arithmetic below is written out literally so that a StateFlags value
// with bit 9 set would go the same place in this port as in retail, instead of
// being quietly folded onto slot 0 or 1.
// ============================================================================
void IVP_SweepEstimate_LedgeVsLedge(IVP_Sweep_Estimator_Context *Ctx)
{
    // :0635fef3-:0635ff35.
    IVP_Ledge_Mindist *const Mindist = static_cast<IVP_Ledge_Mindist *>(Ctx->Mindist);

    const uint32_t TagA = (Mindist->StateFlags >> 8) & 3;
    const uint32_t TagB = ((Mindist->StateFlags ^ 0x100) >> 8) & 3;

    FIVP_Sweep_Mindist_Side *const SlotA = SweepMindistSide(Mindist, TagA);
    FIVP_Sweep_Mindist_Side *const SlotB = SweepMindistSide(Mindist, TagB);

    IVP_Real_Object *const ObjectA = SlotA->Object;
    IVP_Compact_Edge *const FeatureA = SlotA->Feature;

    // :0635ff3c-:0635ff6b. Side A's record: ledge from the tagged edge, points
    // from the ledge, source from the object.
    IVP_Compact_Ledge *const LedgeA = FeatureA->GetCompactLedge();

    FIVP_Sweep_Ledge_Side SideA;
    SideA.Points = LedgeA->GetPointArray();
    SideA.Ledge = LedgeA;
    SideA.Source =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectA->ResolveNarrowPhaseCacheSlot());
    SideA.Object = ObjectA;

    // :0635ff6f-:0635ffa4. Side B's record, the same four fields. Note retail
    // reads SlotB's feature back out of its own frame spill at :0635ff6f rather
    // than from the mindist again; same value.
    IVP_Real_Object *const ObjectB = SlotB->Object;
    IVP_Compact_Edge *const FeatureB = SlotB->Feature;
    IVP_Compact_Ledge *const LedgeB = FeatureB->GetCompactLedge();

    FIVP_Sweep_Ledge_Side SideB;
    SideB.Points = LedgeB->GetPointArray();
    SideB.Ledge = LedgeB;
    SideB.Source =
        reinterpret_cast<FIVP_Sweep_Cache_Source *>(ObjectB->ResolveNarrowPhaseCacheSlot());
    SideB.Object = ObjectB;

    // :0635ffa8.
    Ctx->OutCode = 0;

    // :0635ffaf-:06360060. Two nested switches, on SlotA->ShapeType and then
    // SlotB->ShapeType. Every combination retail does not handle is its own
    // null-store trap, and there are three of them: shape tag 1 on side A
    // accepts only tag 1 on side B, and tag 2 or 3 on side A is rejected
    // outright. That asymmetry is the mindist's own normalization showing
    // through -- the lower-dimensional feature is always put on side A.
    switch (SlotA->ShapeType)
    {
    case 0:
        switch (SlotB->ShapeType)
        {
        case 0:
            // :0636004c-:06360060.
            Ctx->EstimateLedgeVertexVsLedgeVertexContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        case 1:
            // :06360031-:06360045.
            Ctx->EstimateLedgeVertexVsLedgeEdgeContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        case 2:
            // :06360016-:0636002a.
            Ctx->EstimateLedgeVertexVsLedgeFaceContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        default:
            // :0636000a.
            SweepEstimatorUnreachable();
            break;
        }
        break;
    case 1:
        if (SlotB->ShapeType == 1)
        {
            // :0635ffe0-:0635fff4.
            Ctx->EstimateLedgeEdgeVsLedgeEdgeContact(FeatureA, FeatureB, &SideA, &SideB);
        }
        else
        {
            // :0635ffd1.
            SweepEstimatorUnreachable();
        }
        break;
    default:
        // :0635ffbb.
        SweepEstimatorUnreachable();
        break;
    }

    // :06360065-:06360077.
    ObjectA->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SideA.Source));
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        reinterpret_cast<struct IVP_NarrowPhaseCacheSlot *>(SideB.Source));
}

// ============================================================================
// LAB_06360080 -- IVP_SweepEstimate_Unreachable  (decomp-tracker item 3383)
// Common.dll.txt:452624-452625, bytes 0x06360080-0x0636008a, 2 instructions:
// `MOV dword ptr [0x00000000],0x0` then `RET`.
// ============================================================================
void IVP_SweepEstimate_Unreachable(IVP_Sweep_Estimator_Context * /*Ctx*/)
{
    SweepEstimatorUnreachable();
}

// ============================================================================
// FUN_06360090 -- IVP_InitSweepEstimatorTable  (decomp-tracker item 3384)
// Common.dll.txt:452636-452661.
// ============================================================================
void IVP_InitSweepEstimatorTable()
{
    // :06360090-:063600a0. `MOV EAX,LAB_06360080; MOV ECX,0x10;
    // MOV EDI,DAT_0658af9c; REP STOSD` -- sixteen slots of the trap.
    //
    // The array is SIXTEEN dwords, not seventeen. An earlier reading of this
    // function (recorded on item 3384 and in ivp_environment.cpp's now-deleted
    // placeholder) claimed the last explicit store, `MOV [0658afd8],
    // FUN_0635fe30` at :063600fb, lands "one past" the REP STOSD's range. It
    // does not: 0x0658afd8 - 0x0658af9c = 0x3c = 15 * 4, so that store is slot
    // 15, the last of the sixteen the REP already covered. Checked against the
    // raw listing, not the pseudo-decompile.
    for (int Index = 0; Index < 16; ++Index)
    {
        G_SweepEstimatorTable[Index] = &IVP_SweepEstimate_Unreachable;
    }

    // :063600a7-:063600fb, in retail's own store order. Eight of these eleven
    // re-store the trap that the REP STOSD has already written; they are kept
    // because dropping them would hide that retail writes the array
    // exhaustively.
    G_SweepEstimatorTable[3]  = &IVP_SweepEstimate_Unreachable;  // :063600a7
    G_SweepEstimatorTable[4]  = &IVP_SweepEstimate_Unreachable;  // :063600ac
    G_SweepEstimatorTable[6]  = &IVP_SweepEstimate_Unreachable;  // :063600b1
    G_SweepEstimatorTable[7]  = &IVP_SweepEstimate_Unreachable;  // :063600b6
    G_SweepEstimatorTable[8]  = &IVP_SweepEstimate_Unreachable;  // :063600bb
    G_SweepEstimatorTable[9]  = &IVP_SweepEstimate_Unreachable;  // :063600c0
    G_SweepEstimatorTable[10] = &IVP_SweepEstimate_Unreachable;  // :063600c5
    G_SweepEstimatorTable[11] = &IVP_SweepEstimate_Unreachable;  // :063600ca

    G_SweepEstimatorTable[0]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600d4
    G_SweepEstimatorTable[1]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600da
    G_SweepEstimatorTable[2]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600e0
    G_SweepEstimatorTable[5]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600e6

    G_SweepEstimatorTable[12] = &IVP_SweepEstimate_BallVsLedge;  // :063600ec
    G_SweepEstimatorTable[13] = &IVP_SweepEstimate_BallVsLedge;  // :063600f1
    G_SweepEstimatorTable[14] = &IVP_SweepEstimate_BallVsLedge;  // :063600f6

    G_SweepEstimatorTable[15] = &IVP_SweepEstimate_BallVsBall;   // :063600fb
}

// ============================================================================
// `CALL dword ptr [TableIndex*4 + DAT_0658af9c]`, item 3117 at :0634f5ea.
//
// Unguarded, as retail (cmn-ivp3g 2026-10-09 removed an invented range/null
// guard): `ShapeType[j] + ShapeType[i] * 4` with both tags in 0..3 cannot leave
// 0..15, and the table is filled by FUN_06360090 from the environment
// constructor (ivp_environment.cpp, :0634c5c5) before any mindist exists.
// ============================================================================
void IVP_InvokeSweepEstimator(int TableIndex, IVP_Sweep_Estimator_Context *Ctx)
{
    G_SweepEstimatorTable[TableIndex](Ctx);
}
