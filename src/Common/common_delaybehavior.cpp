// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_delaybehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)

// DelayBehavior -- see common_delaybehavior.h for the full RTTI/layout
// writeup this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// FUN_062d0a10 (Common.dll.txt 207935-207956) -- constructor. Calls the
// shared base-init helper FUN_062d2020 first (item 1529 -- NOT the
// "no-op stub" it was long recorded as: it writes +0x04..+0x29, see
// Behavior in common_behavior.h), then default-constructs the still-unidentified
// Watcher sub-object (FULLPORT-DEFERRED, see ConstructDelayWatcher), and
// finally (implicitly, via C++ construction) installs the vtable.
DelayBehavior::DelayBehavior(int InTypeId)
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
    , Watcher2c()   // FUN_062d9b10 (IdleIdea's ctor), .text:062d0a42
    , Field40(0.0f)
    , Field44(0.0f)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

// Vtable slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared
// verbatim across 7 unrelated vtables (decomp-tracker item 1320) -- NO
// DelayBehavior-specific field cleanup exists anywhere in it.
DelayBehavior::~DelayBehavior()
{
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 207972-208004
// (LAB_062d0a70), 31 instructions.
bool DelayBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // retail: `CALL FUN_062d2090(this, Param1, Param2, Param3)` -- the shared
    // Behavior-family context initializer (see common_cruisebehavior.cpp's
    // SetContext for the fully-traced body).
    // 2026-10-08: .text:062d0a86 FUN_062d2090 -> Behavior::BindContext. On success: the
    // IdleIdea's slot 0 (owner, sys, Param2) at 062d0aa0, then +0x40 = +0x44 = 0. AL = the
    // binder's result.
    bool const bOk = BindContext(Param1, Param2, Param3);
    if (bOk)
    {
        Watcher2c.SetContext(OwnerContextPtr14, NavHelperPtr24, Param2);
        Field40 = 0.0f;
        Field44 = 0.0f;
    }
    return bOk;
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 208024-208029
// (FUN_062d0ac0), 6 instructions.
void DelayBehavior::ResetTickState()
{
    Behavior::Slot3();      // 062d0ac3 FUN_062d20d0 (DeltaTime1c = 0 was missing before)
    Field44 = 0.0f;         // 062d0ac8
}

// Own vtable slot 4 (offset 0x10). ICF-shared, literal same retail address
// as IdleBehavior::Disengage() (common_idlebehavior.cpp) -- reproduced here
// with the identical body.
void DelayBehavior::Disengage()
{
    Watcher2c.Commit();     // FUN_062cd5e0: `CALL [EAX+0x10]` on +0x2c (IdleIdea, bare RET)
    Behavior::Slot4();      // tail JMP FUN_062d20f0 (Pathfind->Field_064 = 0 when present, +0x29 = 0)
}

// Own vtable slot 5 (offset 0x14). ICF-shared, literal same retail address
// as CruiseBehavior/IdleBehavior's own SetModeFromFlags().
int DelayBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    ModeSelector0xc = Mode;
    return Mode;
}

// Own vtable slot 7 (offset 0x1c). Common.dll.txt 208050-208081
// (FUN_062d0ae0), 30 instructions.
int DelayBehavior::Update()
{
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return UpdateResult10;
    }

    Watcher2c.Update(DeltaTime1c);   // 062d0afe, result discarded

    // 062d0b01-062d0b12: FLD Field44; FADD DeltaTime1c; FST Field44 (rounded); FCOMP Field40
    // compares the UNROUNDED sum; TEST AH,0x41 / JNZ skips on less, equal or unordered.
    double const Sum = static_cast<double>(Field44) + DeltaTime1c;
    Field44 = static_cast<float>(Sum);
    if (Sum > Field40)
    {
        // Fixed sentinel/reset bit-pattern retail writes verbatim (0xccbebc20).
        unsigned int SentinelBits = 0xccbebc20u;
        Field44 = *reinterpret_cast<float*>(&SentinelBits);

        // retail: `MOV ECX,[ESI+0x20]; PUSH 0x0; PUSH 0x14; CALL FUN_062d29a0`
        // (.text:062d0b14-062d0b22) -- i.e. Slot(param_1) = 0x14, Result(param_2) = 0.
        // 0x14 == 20 is DelayBehavior's own index into IBehaviorManager's 21-entry
        // BehaviorDispatchTable (+0x174, common_iface.h) -- the same "this behavior slot is
        // done" convention GuideBehavior uses with its own index 0xe.
        BehaviorMgrPtr20->SetDirectiveResult(0x14, 0);   // FUN_062d29a0
    }

    UpdateResult10 = 1;
    return UpdateResult10;
}

// Own vtable slot 10 (offset 0x28). Common.dll.txt 208098-208106
// (LAB_062d0b40), 10 instructions.
bool DelayBehavior::ApplyDirective(void const* Param)
{
    float Value = *reinterpret_cast<float const*>(static_cast<unsigned char const*>(Param) + 0x0c);

    Field40 = Value;
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    Field44 = Value; // retail: same incoming value written to BOTH fields, see header comment.

    return true;
}


