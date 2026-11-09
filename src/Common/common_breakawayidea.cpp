// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll BreakAwayIdea (ctor 0x062d8970, Update LAB_062d8a50). See
// src/include/common_idea.h.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_idea.cpp, code unchanged.

#include "common_idea.h"
#include "common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // ------------------------------------------------------------------------
    // BreakAwayIdea::Update's four switch arms (062d8c65/062d8caa/062d8ce9/
    // 062d8d28), which differ from one another in exactly two ways: which pair
    // of components each row's rotation touches, and which of two .rdata
    // doubles supplies the angle.
    // ------------------------------------------------------------------------
    // Each arm is the same three-iteration loop over `EBX = this + 0x60`, with
    // EAX walking +0xc per row, so the operand pair is (row[0], row[2]) when the
    // loop loads `[EAX-8]` and (row[1], row[2]) when it loads `[EAX-4]`. In both
    // cases the SECOND component is read back out of memory AFTER the first has
    // been stored, and the two stores land at different addresses, so the second
    // formula genuinely uses the ORIGINAL value of both operands -- which is why
    // these are written with locals rather than in place.
    //
    // The angles are .rdata doubles decoded from their raw bytes, not from
    // Ghidra's truncated inline comment:
    //   DAT_063a0518 = 40 00 C1 52 40 00 00 00 (LE qword 0x4000C15240000000)
    //                = +2.0943951023931953 == +2*pi/3
    //   DAT_063a0510 = C0 00 C1 52 40 00 00 00 = -2.0943951023931953 == -2*pi/3
    // Retail computes FCOS and FSIN on the double, keeps both on the x87 stack for
    // the whole loop, and forms every product and sum there: only the two FSTPs per
    // row round to float. So Cos/Sin and the arithmetic are double here.

    // caseD_1 / caseD_2: x' = x*Cos - Sin*z, z' = Cos*z + x*Sin.
    void RotateFrameRowsXZ(Matrix& M, double Angle)
    {
        double const Cos = std::cos(static_cast<double>(Angle));   // FCOS, ST2 in the loop
        double const Sin = std::sin(static_cast<double>(Angle));   // FSIN, ST1 in the loop
        for (int Row = 0; Row < 3; ++Row)
        {
            double const X = M.E[Row][0];
            double const Z = M.E[Row][2];
            M.E[Row][0] = static_cast<float>(X * Cos - Sin * Z);   // FSUBP, FSTP [EAX-0x14]
            M.E[Row][2] = static_cast<float>(Cos * Z + X * Sin);   // FADDP, FSTP [EAX-0x0c]
        }
    }

    // caseD_3 / caseD_4: y' = y*Cos + Sin*z, z' = Cos*z - y*Sin.
    void RotateFrameRowsYZ(Matrix& M, double Angle)
    {
        double const Cos = std::cos(static_cast<double>(Angle));
        double const Sin = std::sin(static_cast<double>(Angle));
        for (int Row = 0; Row < 3; ++Row)
        {
            double const Y = M.E[Row][1];
            double const Z = M.E[Row][2];
            M.E[Row][1] = static_cast<float>(Y * Cos + Sin * Z);   // FADDP, FSTP [EAX-0x10]
            M.E[Row][2] = static_cast<float>(Cos * Z - Y * Sin);   // FSUBP, FSTP [EAX-0x0c]
        }
    }
}

// ----------------------------------------------------------------------------
// BreakAwayIdea -- common_buzzbehavior.h's "WatcherE".
// ----------------------------------------------------------------------------

// FUN_062d8970 (item 1628), Common.dll.txt 221182-221239, 57 instructions.
BreakAwayIdea::BreakAwayIdea()
    : BaseIdea()
    , Picker()
    , BreakStyle(0)                         // 062d89f2
    , Field38(350.0f)                    // 0x43af0000, 062d8a12
    , Flag3c(1)                          // 062d8a19
    , Pad3d{}
    , Throttle(1.0f)                      // 062d89d7
    , Flag44(0)                          // 062d89f5
    , Pad45{}
    , AttemptReverseElapsed(0.0f)                      // 062d89f8 (retail stores an integer 0)
    , AttemptReverseTime(4.0f)                      // 0x40800000, 062d8a1d
    , Field50(0.0f)                      // 062d89fb (retail stores an integer 0)
    , ReverseDistance(100.0f)                    // 0x42c80000, 062d8a2b
    , Field58(6.0f)                      // 0x40c00000, 062d8a24
    , Flag5c(0)                          // 062d89fe
    , Pad5d{}
    , Frame60(0.0f, 0.0f, 0.0f,          // +0x60: NOT touched by retail's ctor --
              0.0f, 0.0f, 0.0f,          // Update()'s first reroll always fills it
              0.0f, 0.0f, 0.0f)          // before anything reads it. Zeroed here per
                                          // this marathon's "safe over-init" convention.
    , IntervalElapsed(100000.0f)                 // 0x47c35000, 062d89ec
    , IntervalTime(100000.0f)                 // 0x47c35000, 062d89e6
    , AfterburnerDelay(0.0f)                      // 062d8a01 (retail stores an integer 0)
    , AfterburnerElapsed(0.0f)                      // 062d8a07 (retail stores an integer 0)
    , TurnThrottle(1.0f)                      // 062d89da
    , RollThrottle(1.0f)                      // 062d89e0
{
    Picker.Flag1c = 1;   // 062d8a0e -- the ONE constant that differs from DodgeIdea
}

// Own vtable slot 1 -- LAB_062d8a50, item 1629, 634 instructions
// (Common.dll.txt:221255-221928). PORTED IN FULL 2026-09-17
// (claude:common-idea-updates). Every instruction is accounted for and every
// branch traced; no Ghidra decompile exists for this function, so all of it
// comes off the raw listing.
//
// Receiver settled by RTTI, not by position: the ctor above installs
// .rdata:PTR_LAB_063a04f8 at 062d89b6; [vtable-4] == 063a04f4 -> COL 063a9b40,
// whose +0x0C at .rdata:063a9b4c is .data:BreakAwayIdea::RTTI_Type_Descriptor;
// slot 1 of that table (.rdata:063a04fc) is LAB_062d8a50, and that slot is its
// ONLY xref. The nearby .rdata:063a9b54 DodgeMissileIdea descriptor belongs to
// the NEXT COL in the array and is what an earlier guess mistook for this one.
//
// FRAME MAP, since everything else hangs off it: `SUB ESP,0xac` plus four
// pushes (EBX/EBP/ESI/EDI), so with S == ESP after the prologue, DeltaTime sits
// at S+0xc0 -- which is exactly where every `FLD [ESP+0xc0]` in the body reads.
// EBP is `this` for the whole body (062d8a59). EDI is the literal 0 and nothing
// else: it is zeroed at 062d8a5f, clobbered by the reroll's REP MOVSD, and
// re-zeroed at 062d8d73 before its next use -- so `SUB EAX,EDI` reads as a
// plain `EAX` and none of it survives into this port.
//
// CONTEXT IS THE IBehaviorManager. 062d8a77 does `MOV EAX,[EBP+0xc]` /
// `MOV ECX,[EAX+0xc]` and hands the result to PathfindManager::GetUserZoneApproachDirection
// (FUN_062ddcd0), whose receiver is `*(IBehaviorManager + 0x0c)`; and
// 062d8b23 / 062d9022 use the very same `[EBP+0xc]` as the receiver of
// IBehaviorManager::get_ship_up_direction. Two independent uses, one type.
//
// THE ARGUMENT-COUNT TRAP, once, for both of this body's steering tails:
// `?get_ship_up_direction@IBehaviorManager@@QAE?BVVector@@XZ` is __thiscall
// taking VOID and returning a Vector by value, so it consumes only its own
// hidden return-slot pointer and pops it itself. The `PUSH 1` and the two
// pointer pushes that appear to precede it at 062d8b1c-062d8b2f are
// ComputeBankedSteeringFrame's arguments 5/4/3, pushed ahead of the call that
// produces its argument 2. That is the only reading under which the
// `ADD ESP,0x14` at 062d8b45 balances, and it is why these sites clean 0x14
// rather than 0x18.
int BreakAwayIdea::Update(float DeltaTime)
{
    // 062d8a5b-062d8a74.
    if (Object == nullptr)
    {
        return 3;
    }

    // Context is the IBehaviorManager; Handle, its Nav block, the +0x84 owner and
    // Handle + 0x18 are all re-derived at every use by retail and never tested.
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);

    // 062d8a77-062d8a89: ask the AI volume probe whether anything has to be
    // avoided right now. A ZERO return means "yes, go this way instead" and
    // takes over the whole tick; anything else falls through to the real
    // break-away logic. ECX = [Context + 0xc] (IBehaviorManager::Pathfind), unguarded.
    Vector ProbeDir;
    if (Manager->Pathfind->GetUserZoneApproachDirection(&ProbeDir) == 0)
    {
        // 062d8a8f-062d8ab3: a look-at basis aimed down the probe's direction,
        // with no up hint (the `PUSH EDI` at 062d8a9f is a null UpHint).
        Matrix Frame;
        BuildOrientationBasis(Frame, Handle->SelfTarget.Iface->get_transform().Rotation,
                              ProbeDir, nullptr);

        // 062d8ab8-062d8ace: jitter it. 0x3e490fdb == 0.19634954f == pi/16, and
        // the bank angle is the same `PUSH EDI` zero.
        Matrix Spread;
        BuildRandomSpreadFrame(Spread, Frame, 0.19634954f, 0.0f);

        // 062d8ad3-062d8b03. The fourth argument is `&S+0x28`, a dword retail
        // zeroes at 062d8add and reads back as a float at 062d8b08 -- i.e. the
        // optional `float* OutAngle` out-param, not a flag.
        float TurnAngle = 0.0f;
        Vector AxisThrottle = TurnHelper::get_angular_throttle(
            Handle->SelfTarget.Iface->get_transform().Rotation, Spread, &TurnAngle);

        // 062d8b08-062d8b6b. .rdata:DAT_0639f534 is db 0f 49 3e == 0x3e490fdb ==
        // pi/16 -- the same constant the spread uses, decoded from the raw bytes
        // (Ghidra prints it truncated as "= DBh"). `TEST AH,0x41; JNZ` takes the
        // not-greater case away, so the steering arm is the strictly-greater one.
        Vector Command{ 0.0f, 0.0f, 0.0f };
        if (TurnAngle > 0.19634954f)
        {
            Vector const Up = Manager->get_ship_up_direction();         // 062d8b30
            ComputeBankedSteeringFrame(&Handle->SelfTarget, Up, Spread, AxisThrottle, 1); // 062d8b40
            Command = AxisThrottle;                                      // 062d8b48 pushes &AxisThrottle
        }
        // else: 062d8b4f zero-fills a separate slot and passes THAT, so the
        // small-turn arm really does command a zero vector rather than the
        // unsteered throttle.

        // 062d8b6c-062d8b87, then the shared `XOR EAX,EAX` exit at 062d8b88.
        Handle->Thrust.SetAxisThrottle(Command);
        Handle->Thrust.SetThrottle(1.0f, 0);
        return 0;
    }

    // ------------------------------------------------------------------------
    // LAB_062d8b97: the reroll timer. `FLD DeltaTime; FADD IntervalElapsed; FST IntervalElapsed;
    // FCOMP IntervalTime` compares the UNROUNDED sum. `TEST AH,0x1; JNZ` is the C0-only
    // test, so it branches away on strictly-less (and unordered) and the reroll
    // runs on >=. Both fields start at 100000.0f, so the very first tick always
    // rerolls; Commit() restores IntervalElapsed to 100000.0f to re-arm that.
    // ------------------------------------------------------------------------
    double const RerollClock = static_cast<double>(DeltaTime) + IntervalElapsed;
    IntervalElapsed = static_cast<float>(RerollClock);
    if (RerollClock >= IntervalTime)
    {
        // 062d8bbb-062d8c4f: rebuild Frame60 as a look-at basis pointing the
        // OPPOSITE way to the owner's own heading, with world up as the hint.
        // The three raw reads at +0x8/+0x14/+0x20 are column 2 (MatrixColumn2's
        // own stride), and all three are negated before the Vec3Assign. The owner's
        // get_transform() is fetched twice (062d8be5, 062d8c2a).
        Vector const WorldUp{ 0.0f, 1.0f, 0.0f }; // 062d8bbe-062d8bce: {0, 1.0f, 0}
        Vector Backward;
        Vector const Col2 = MatrixColumn2(Handle->SelfTarget.Iface->get_transform().Rotation);
        Vec3Assign(&Backward, -Col2.x, -Col2.y, -Col2.z);
        Matrix Basis;
        BuildOrientationBasis(Basis, Handle->SelfTarget.Iface->get_transform().Rotation,
                              Backward, &WorldUp);
        // 062d8c3b-062d8c47: `MOV ECX,9; REP MOVSD` into `this + 0x60`.
        Frame60 = Basis;

        // 062d8c4c-062d8c5e: `LEA ECX,[EBP+0x14]` is &Picker, and FUN_062d76d0
        // is GenericWeightedPick -- whose documented layout (a 1..4 tag followed
        // by four float weights) is FDirectionPicker's LastDirection/WeightLeft..WeightDown
        // exactly. `DEC EAX; CMP EAX,3; JA default` gives the four arms below.
        int const Pick = GenericWeightedPick(&Picker.LastDirection);
        switch (Pick)
        {
        case 1:
            RotateFrameRowsXZ(Frame60, 2.0943951023931953); // DAT_063a0518, +2*pi/3
            break;
        case 2:
            RotateFrameRowsXZ(Frame60, -2.0943951023931953); // DAT_063a0510, -2*pi/3
            break;
        case 3:
            RotateFrameRowsYZ(Frame60, 2.0943951023931953); // DAT_063a0518
            break;
        case 4:
            RotateFrameRowsYZ(Frame60, -2.0943951023931953); // DAT_063a0510
            break;
        default:
            break; // switchD_062d8c5e's JA arm falls straight to 062d8d69
        }

        IntervalElapsed = 0.0f; // 062d8d69, reached by every arm including the default
    }

    // ------------------------------------------------------------------------
    // LAB_062d8d75: three modes, and "anything else" returns 0.
    // ------------------------------------------------------------------------
    if (BreakStyle == 2)
    {
        // 062d8d8e-062d8d99. `TEST AH,0x5` with `JP` takes the
        // greater-or-unordered case away, so the helper only runs while the
        // AttemptReverseElapsed timer is strictly below its AttemptReverseTime cap.
        if (!(AttemptReverseElapsed < AttemptReverseTime))
        {
            // LAB_062d8e18.
            AttemptReverseElapsed = 0.0f;
            Field50 = 0.0f;
            Flag5c = 0;
            return 0;
        }

        // 062d8d9b-062d8daa: `MOV ECX,EBP` -- the helper runs on this very
        // object. See the declaration in common_idea.h for why that is the whole
        // proof of its receiver.
        int const Clearance = EvaluateBreakAwayClearance();

        if (Clearance == 0)
        {
            // LAB_062d8de8. `FLD DeltaTime; FADD Field50; FST Field50; FCOMP Field58`
            // compares the unrounded sum; `TEST AH,0x41; JNZ` keeps waiting on
            // not-greater (or unordered).
            double const Waited = static_cast<double>(DeltaTime) + Field50;
            Field50 = static_cast<float>(Waited);
            if (!(Waited > Field58))
            {
                return 0;
            }
            Field50 = 0.0f;
            return 1;
        }
        if (Clearance == 1)
        {
            // LAB_062d8dd0.
            AttemptReverseElapsed = 0.0f;
            Field50 = 0.0f;
            return 1;
        }
        if (Clearance != 2)
        {
            return 0; // 062d8daa
        }

        // 062d8db0-062d8dcd.
        Flag5c = 0;
        AttemptReverseElapsed += DeltaTime;
        return 0;
    }

    if (BreakStyle == 1)
    {
        // LAB_062d8e31.
        Handle->Thrust.SetThrottle(Throttle, 0);   // 062d8e3c

        // 062d8e41-062d8e6f: the default aim is this object's OWN reversed
        // heading -- note the receiver here is `this->Object`, not the nav
        // block's owner.
        Vector Aim{ 0.0f, 0.0f, 0.0f };
        {
            Vector const Col2 = MatrixColumn2(ObjInspectOf(Object)->get_transform().Rotation);
            Aim.x = -Col2.x;
            Aim.y = -Col2.y;
            Aim.z = -Col2.z;
        }

        // 062d8e73-062d8f16. Retail tests the watcher twice (`== 0`, then
        // `- 8 == 0`); the second can only fire on the impossible raw value 8 and
        // is folded into the single null check here. Picker.Flag1c picks the
        // sign: BreakAwayIdea's ctor sets it to 1 (negate), DodgeIdea's to 0.
        IObjRW* const Watched = WatchedObjectOf(Picker.Watcher);
        if (Watched != nullptr)
        {
            Vector const Col2 = MatrixColumn2(ObjInspectOf(Watched)->get_transform().Rotation);
            if (Picker.Flag1c != 0)
            {
                Aim.x = -Col2.x;   // 062d8e9e-062d8ed7
                Aim.y = -Col2.y;
                Aim.z = -Col2.z;
            }
            else
            {
                Aim = Col2;        // 062d8eed-062d8f16, NOT negated
            }
        }

        // LAB_062d8f1a, 062d8f1a-062d8f7f. .rdata:DAT_06399408 is four zero
        // bytes == 0.0f, and the accumulation order is x, then z, then y --
        // preserved, in double (nothing is stored before the FCOMP). `TEST AH,0x41;
        // JNZ` branches away on not-greater (or unordered).
        Vector OwnerBack;
        {
            Vector const Col2 = MatrixColumn2(Handle->SelfTarget.Iface->get_transform().Rotation);
            Vec3Assign(&OwnerBack, -Col2.x, -Col2.y, -Col2.z);   // 062d8f50
        }
        double const Facing = (static_cast<double>(OwnerBack.x) * Aim.x + static_cast<double>(OwnerBack.z) * Aim.z)
                              + static_cast<double>(OwnerBack.y) * Aim.y;

        if (Facing > 0.0)
        {
            // 062d8f85-062d905d: the steering tail. The fourth argument of
            // get_angular_throttle is `PUSH EDI` here -- a genuine null
            // OutAngle, not the zeroed-dword pointer the avoid path passes.
            Vector Command = TurnHelper::get_angular_throttle(
                Handle->SelfTarget.Iface->get_transform().Rotation, Frame60, nullptr);

            // 062d8fa7-062d8ff1: one FLD of TurnThrottle feeding three FMUL ST1, with
            // each product stored back into the same slot AND copied into the
            // by-value Vector argument below.
            Command.x *= TurnThrottle;
            Command.y *= TurnThrottle;
            Command.z *= TurnThrottle;

            // 062d8ff4-062d900c. Push order gives TurnAngle = 0.0f
            // (`PUSH EDI`), ReferenceAngle = 1.0f (`PUSH 0x3f800000`) and
            // bBankBlend = true (`PUSH 0x1`); total cleanup 0x24 == 4 + 4 + 12 +
            // 4 + 4 + 4 + 4, which is what fixes the by-value Vector's position.
            Vector Limited{ 0.0f, 0.0f, 0.0f };
            LimitAxisThrottleForTurn(Limited, ObjInspectOf(Object), Command, &Frame60,
                                     0.0f, 1.0f, true);
            Command = Limited;

            // 062d9016-062d9039: Mode is `PUSH EDI` == 0 here, where the avoid
            // path above passes 1.
            Vector const Up = Manager->get_ship_up_direction();                    // 062d9029
            ComputeBankedSteeringFrame(&Handle->SelfTarget, Up, Frame60, Command, 0); // 062d9039

            // 062d903e-062d905d.
            Handle->Thrust.SetAxisThrottle(Command);
            return 0;
        }

        // LAB_062d9060, 062d9060-062d9102: no steering, just drive forward at
        // RollThrottle along the owner's own frame. Push order is
        // `PUSH RollThrottle; PUSH 0; PUSH 0`, so the by-value Vector is
        // {0, 0, RollThrottle} in memory order.
        Vector Drive = TransformVectorByMatrix(
            Handle->SelfTarget.Iface->get_transform().Rotation,
            Vector{ 0.0f, 0.0f, RollThrottle });                                       // 062d9081
        Handle->Thrust.SetAxisThrottle(Drive);    // 062d9094

        // 062d9099-062d90c5: `FST AfterburnerElapsed; FCOMP AfterburnerDelay` compares the unrounded
        // sum; `TEST AH,0x1; JNZ` skips on strictly-less (or unordered), so the
        // notify fires once AfterburnerElapsed reaches AfterburnerDelay.
        double const Elapsed = static_cast<double>(DeltaTime) + AfterburnerElapsed;
        AfterburnerElapsed = static_cast<float>(Elapsed);
        if (Elapsed >= AfterburnerDelay)
        {
            Handle->Thrust.SetThrustersActive(true);              // 062d90c1
        }

        // LAB_062d90c6-062d9102. Note this arm does NOT null-check the watcher
        // before the range test -- unlike the BreakStyle == 0 arm below, which
        // does. Kept as retail has it.
        IObjRW* const RangeTarget = WatchedObjectOf(Picker.Watcher);
        if (IsWithinInterceptRangeOfIface(&Handle->SelfTarget, RangeTarget, Field38))
        {
            return 0;
        }
        return 1;
    }

    if (BreakStyle != 0)
    {
        return 0; // 062d8d88, the `JNZ LAB_062d8b88` arm
    }

    // ------------------------------------------------------------------------
    // LAB_062d9105: BreakStyle == 0. Same "drive forward" shape as the tail above,
    // plus the cruise gate and a watcher null check the other arm lacks.
    // ------------------------------------------------------------------------
    // 062d9108-062d9118 zero the slot first; 062d9141 FUN_062e2c00 then fills it.
    Vector Drive = TransformVectorByMatrix(
        Handle->SelfTarget.Iface->get_transform().Rotation,
        Vector{ 0.0f, 0.0f, RollThrottle });

    // 062d915a-062d916f: retail fetches get_transform() a SECOND time here
    // and discards the result outright (EAX is overwritten by the `LEA` at
    // 062d9175 before anything reads it). Reproduced rather than dropped --
    // slot 0xc is a plain accessor, so this costs nothing but it is really
    // in the shipped code.
    (void)Handle->SelfTarget.Iface->get_transform();

    Handle->Thrust.SetAxisThrottle(Drive);   // 062d917d
    Handle->Thrust.SetThrottle(Throttle, 0);  // 062d918d

    // 062d9192-062d91b0: unrounded sum compared (FST then FCOMP), TEST AH,0x1 + JNZ
    // skips on strictly-less or unordered.
    double const Elapsed = static_cast<double>(DeltaTime) + AfterburnerElapsed;
    AfterburnerElapsed = static_cast<float>(Elapsed);
    if (Elapsed >= AfterburnerDelay)
    {
        Handle->Thrust.SetThrustersActive(true);               // 062d91ba
    }

    // LAB_062d91bf-062d9202. The two `JZ LAB_062d8b88` tests are the watcher
    // null check; after them retail re-reads the same field and re-tests it a
    // third time, which is why the listing has two nearly-identical call setups
    // (062d91dc and 062d91f2) that both jump back to the shared call at
    // LAB_062d90e3. Only the first is reachable.
    IObjRW* const RangeTarget = WatchedObjectOf(Picker.Watcher);
    if (RangeTarget == nullptr)
    {
        return 0;
    }
    if (IsWithinInterceptRangeOfIface(&Handle->SelfTarget, RangeTarget, Field38))
    {
        return 0;
    }
    return 1;
}

// FUN_062d9220, item 1630, Common.dll.txt:221938-222099 (138 instructions).
// PORTED IN FULL 2026-09-17 (claude:common-idea-updates).
//
// RECEIVER: see the declaration in common_idea.h. In short -- exactly one
// caller module-wide (.text:062d8d9d), reached with `MOV ECX,EBP` where EBP is
// BreakAwayIdea::Update's own `this`, and the address appears in no vtable. The
// item's original note treated its fields as an anonymous "SteeringTask
// composite" with "no owning C++ class identified"; that is retired, and every
// one of them resolves against this class: +0x28 is Picker.Watcher, +0x54 is
// ReverseDistance, +0x5c is Flag5c, +0x08 is BaseIdea::Handle and +0x04 is
// BaseIdea::Object.
//
// Frame: `SUB ESP,0xc` plus two pushes, so with B == ESP after the prologue the
// single 12-byte scratch Vector is at B+0x8..B+0x13 -- the three dwords Ghidra
// calls local_c/local_8/local_4. It is used twice, for two different callees.
//
// Returns, all three of which BreakAwayIdea::Update's BreakStyle == 2 arm
// distinguishes: 2 = nothing usable to break away from (or the geometry does not
// qualify yet), 1 = clear, stop breaking away, 0 = still running.
int BreakAwayIdea::EvaluateBreakAwayClearance()
{
    // 062d9227-062d923f: the `EAX ? EAX - 8 : 0` downcast, and its result being
    // zero is the early-out. Retail writes it as two tests (`TEST EAX,EAX` then
    // `ADD EAX,-8; TEST EAX,EAX`), the second of which can only fire on the raw
    // value 8.
    IObjRW* const Watched = WatchedObjectOf(Picker.Watcher);
    if (Watched == nullptr)
    {
        return 2;
    }

    // 062d9240-062d92a9. Only evaluated while Flag5c is clear; BL survives from
    // here to 062d932b, which is the one place it is read again.
    //
    // 0xbf7ff972 == -0.9999f, decoded from the raw push immediate: the cone
    // half-angle is about 179.2 degrees, i.e. "the watched object is almost
    // exactly dead astern of us". The receiver of the zero-argument vcall at
    // 062d9262 is the watched object and the slot is 0 -- IObjInspect's
    // `Vector const& get_position()`.
    bool bNearlyAstern = false;
    if (Flag5c == 0)
    {
        bNearlyAstern = IsPointOutsideHeadingCone(ObjInspectOf(Watched)->get_position(),
                                                  ObjInspectOf(Object),
                                                  BitsToFloat(0xbf7ff972u));

        // 062d9271-062d92a5: a 12-byte scratch whose x is 1.0f on the true arm
        // and 0.0f on the false arm, y and z always zero.
        Vector const Sync{ bNearlyAstern ? 1.0f : 0.0f, 0.0f, 0.0f };
        Handle->Thrust.SetNudgeDirection(Sync);   // 062d92a5, Handle+0x18 untested
    }

    // LAB_062d92aa, 062d92aa-062d92cd: the SAME scratch, re-zeroed, handed to a
    // different helper. Always runs, Flag5c or not.
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetAxisThrottle(ZeroVector);   // 062d92cd

    // 062d92d2-062d92f7.
    // The ST0 return stays unrounded until the FCOMP / FSTP ST0 below.
    double const Clearance = NavInterceptClearanceOfIface(&Handle->SelfTarget, Watched);

    if (Flag5c == 0)
    {
        // 062d92f9-062d9301: `TEST AH,0x5` with `JP` leaves only the strictly-
        // less case falling through, so we proceed only while the clearance is
        // still below the ReverseDistance threshold.
        if (!(Clearance < ReverseDistance))
        {
            Flag5c = 0; // LAB_062d932f
            return 2;
        }

        // 062d9303-062d932d. 0xbf34fdf4 == -0.707f -- a wider, ~135-degree
        // cone than the first test's. Note this is NOT quite -sqrt(2)/2
        // (0xbf3504f3); decoded from the raw immediate, not rounded to it.
        bool const bAstern = IsPointOutsideHeadingCone(ObjInspectOf(Watched)->get_position(),
                                                       ObjInspectOf(Object),
                                                       BitsToFloat(0xbf34fdf4u));
        if (!bAstern || bNearlyAstern)
        {
            Flag5c = 0; // LAB_062d932f
            return 2;
        }
    }
    // else: LAB_062d933e is a bare `FSTP ST0`, i.e. the clearance is computed
    // and then discarded once Flag5c is already latched.

    // LAB_062d9340, 062d9340-062d9355. 0xbf800000 == -1.0f.
    Flag5c = 1;                                                      // 062d934d
    Handle->Thrust.SetThrottle(-1.0f, 0);    // 062d9351

    // 062d9356-062d9399: the same two-step watcher test as the prologue's,
    // repeated -- retail re-reads `this+0x28` rather than reusing the value.
    IObjRW* const Target = WatchedObjectOf(Picker.Watcher);
    if (Target == nullptr)
    {
        return 0; // LAB_062d939a
    }

    // 062d9372-062d9389. FUN_062e5190 is IsAlignedAndOutsideHeadingCone (item
    // 1754, ported this same pass -- it was `simplified` with no C++ body when
    // this function was picked up). The third argument is `PUSH 0x0`, i.e. a
    // 0.0f cosine threshold: "we are past the target's beam".
    if (!IsAlignedAndOutsideHeadingCone(&Handle->SelfTarget, Target, 0.0f))
    {
        return 0;
    }

    Flag5c = 0; // 062d938b
    return 1;
}

// FUN_062d9450 (item 1635), Common.dll.txt 222186-222209, 16 instructions,
// __thiscall RET 4. Receiver proof and the instruction-by-instruction decode are
// in common_idea.h's declaration comment; the receiver is BreakAwayIdea, settled
// by .text:062c6c8e's `LEA EDI,[ESI+0x258]` and re-confirmed by the vtable
// dispatch two instructions later at .text:062c6cbc.
//
// `BaseWatcher::set()` is used rather than the protected `set_pointer()` because
// set() IS the `if (Target != Watched)` dedup retail open-codes at 062d9465 --
// see common_basewatcher.cpp. The two are byte-equivalent here.
void BreakAwayIdea::SetWatchObject(void const* Object2)
{
    // 062d9454-062d9463: the IObjRW -> Watchable adjustment, with retail's own
    // null arm folding to a plain nullptr.
    Watchable const* const Target =
        (Object2 != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
            : nullptr;

    Picker.Watcher.set(Target);  // 062d9465-062d946a
    Picker.Flag1c = 0;           // 062d946f, unconditional
}

// Own vtable slot 4 -- LAB_062d93e0, Common.dll.txt 222156-222184,
// 23 instructions.
void BreakAwayIdea::Commit()
{
    IntervalElapsed = 100000.0f;   // 062d93f1, restores the ctor default
    AfterburnerElapsed = 0.0f;        // 062d93fb (retail stores an integer 0)

    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };                                   // 062d9405-062d9415
    Handle->Thrust.SetAxisThrottle(ZeroVector); // 062d941d

    // 062d9422-062d9446: `[Handle+0x14fc][+0x84]->vtbl[0xbc]()`, and slot 0xbc of
    // IObjInspect is is_player() -- so retail suppresses this notification on the
    // human player's own ship. Neither Handle nor the +0x84 pointer is tested.
    if (!Handle->SelfTarget.Iface->is_player())
    {
        Handle->Thrust.SetThrustersActive(false);   // 062d9446
    }
}
