// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_drasticevadebehavior.h"

#include <cstring>

// DrasticEvadeBehavior -- see common_drasticevadebehavior.h. Ported from
// EXE/Disasm/Common.dll.txt; every method below re-traced 2026-10-08.

// FUN_062c8730 (Common.dll.txt 195469-195488), all 15 instructions: FUN_062d2020(TypeId),
// +0x40 = 0, vptr, +0x30 = 4.0f (0x40800000), +0x34/+0x38/+0x3c = 0. +0x2c is left to
// Slot3 in retail; the port zeroes it (Slot3 runs before Slot7 reads it).
DrasticEvadeBehavior::DrasticEvadeBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , Opaque18{}
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , StateTimer2c(0.0f)
    , DurationThreshold30(4.0f)
    , TargetOffset{}
    , TargetKind40(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

// Slot 0 is FUN_062c7090, the scalar-deleting dtor ICF-shared by seven behavior vtables.
DrasticEvadeBehavior::~DrasticEvadeBehavior()
{
}

// Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
bool DrasticEvadeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// Slot 3, FUN_062c8790: `CALL FUN_062d20d0; MOV dword [ESI+0x2c],0; RET`.
void DrasticEvadeBehavior::Slot3()
{
    Behavior::Slot3();
    StateTimer2c = 0.0f;
}

// Slot 4, FUN_062c87b0: `CALL FUN_062d20f0; OR byte [ESI+0x28],0x2; RET`.
void DrasticEvadeBehavior::Slot4()
{
    Behavior::Slot4();
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
}

// Slot 5, LAB_062c8760: EAX = 2; `TEST AL,DL` (AL = 2, DL = Flags28) -- bit 0x2.
//   set:   `XOR EAX,EAX; CMP DL,AL` with DL = Flag29 -> JZ (Flag29 == 0) keeps EAX = 0.
//   clear: `TEST DL,DL` -> JZ (Flag29 == 0) keeps EAX = 2.
//   Not taken (Flag29 != 0) -> EAX = 3. Then [ECX+0xc] = EAX, RET 4 (EAX returned).
int DrasticEvadeBehavior::Slot5(float Unused)
{
    (void)Unused;
    int Result = 2;
    if ((Flags28 & 0x2) != 0)
        Result = 0;
    if (Flag29 != 0)
        Result = 3;
    ModeSelector0xc = Result;
    return Result;
}

// Slot 7, LAB_062c8800 (Common.dll.txt 195590-195666), 76 instructions.
int DrasticEvadeBehavior::Slot7()
{
    if (TargetKind40 == 0)
    {
        // LAB_062c8864: `FLD [+0x1c]; FADD [+0x2c]; FST [+0x2c]; FCOMP [+0x30]`,
        // `TEST AH,0x5; JP` -- not taken only for C0 alone, i.e. timer < duration.
        // The compare uses the unrounded sum still in ST0 (FST, not FSTP).
        double const Timer = static_cast<double>(DeltaTime1c) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        IObjAffect* const Affect = static_cast<IObjRW*>(OwnerContextPtr14);   // [+0x14]+4
        if (Timer < static_cast<double>(DurationThreshold30))
        {
            Vector const Nudge = { TargetOffset.x, TargetOffset.y, 0.0f };
            Affect->set_nudge_vec(Nudge);                                   // 062c889a [EAX+0x8]
            UpdateResult10 = 1;
            return 1;
        }
        Vector const Zero = { 0.0f, 0.0f, 0.0f };
        Affect->set_nudge_vec(Zero);                                        // 062c88c9
        UpdateResult10 = 2;
        return 2;
    }

    if (TargetKind40 == 1)
    {
        // LAB_062c881e: same accumulate/compare; FUN_062e89a0 on [+0x24]+0x18 with Mode 0
        // pushed first, then -1.0f (0xbf800000) while running or 0 when expired.
        double const Timer = static_cast<double>(DeltaTime1c) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        ThrustManager* const Thrust = &NavHelperPtr24->Thrust;
        if (Timer < (double)DurationThreshold30)
        {
            Thrust->SetThrottle(-1.0f, 0);
            UpdateResult10 = 1;
            return 1;
        }
        Thrust->SetThrottle(0.0f, 0);
        UpdateResult10 = 2;
        return 2;
    }

    UpdateResult10 = 3;                                                     // 062c8811
    return 3;
}

// Slot 10, LAB_062c87c0 (Common.dll.txt 195559-195584), all 26 instructions.
bool DrasticEvadeBehavior::Slot10(void* DirectiveOp)
{
    unsigned char const* const Op = static_cast<unsigned char const*>(DirectiveOp);
    std::memcpy(&TargetKind40, Op + 0xc, sizeof(TargetKind40));             // 062c87c9
    if (TargetKind40 == 0)
        std::memcpy(&TargetOffset, Op + 0x10, sizeof(TargetOffset));         // 062c87d0-062c87e4
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);                     // 062c87f0
    std::memcpy(&DurationThreshold30, Op + 0x1c, sizeof(DurationThreshold30)); // 062c87f3
    return true;
}
