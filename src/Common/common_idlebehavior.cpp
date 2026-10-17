// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_idlebehavior.h"

// IdleBehavior -- see common_idlebehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// ----------------------------------------------------------------------------
// FUN_062cd530 (Common.dll.txt 202691-202720) -- constructor. Calls the
// shared TractorArm-family base-init helper FUN_062d2020 first (item 1529,
// already a REAL field-initialising base ctor -- it writes +0x04..+0x29 (see Behavior in common_behavior.h); the long-recorded "no-op stub" reading was wrong),
// then default-constructs the still-unidentified IdleWatcher sub-object
// (FULLPORT-DEFERRED, see ConstructIdleWatcher), and finally (implicitly,
// via the base class list + this being a real C++ object) installs the
// vtable -- retail's own `MOV dword ptr [ESI], PTR_FUN_0639fc5c` needs no
// manual equivalent.
IdleBehavior::IdleBehavior(int InTypeId)
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
    , Watcher2c()   // FUN_062d9b10 -- IdleIdea's ctor (identified 2026-10-08)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

// FUN_062c7090 (Common.dll.txt 193345-193362) is MSVC's scalar-deleting-
// destructor boilerplate: it tail-calls a shared teardown helper
// (thunk_FUN_062d2080) and conditionally frees the block -- NO
// IdleBehavior-specific field cleanup exists anywhere in it (confirmed: this
// exact thunk address is shared verbatim across 7 unrelated vtables,
// decomp-tracker item 1320, matching the same "empty real dtor" precedent
// already documented for InstantTradeLaneBehavior). Nothing for this port's
// destructor to do.
IdleBehavior::~IdleBehavior()
{
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 202736-202765
// (LAB_062cd590), 28 instructions.
bool IdleBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // retail: `CALL FUN_062d2090(this, Param1, Param2, Param3)` -- the shared
    // Behavior-family context initializer (see common_dockbehavior.cpp's
    // DockBehavior::SetContext for the fully-traced body; reproduced inline
    // here again since IdleBehavior does not share a header with
    // DockBehavior and the two call sites' field targets, while offset-
    // identical, are genuinely different C++ members):
    //   if (Param1 == nullptr || Param3 == nullptr) return false;
    //   OwnerContextPtr14 = Param1;
    //   CachedHandle08 = Param1->vtbl[0x20](Param1);
    //   NavHelperPtr24 = Param3;
    //   BehaviorMgrPtr20 = Param2;
    //   return true;
    // 2026-10-08: .text:062cd5a6 FUN_062d2090 -> Behavior::BindContext; on success
    // (062cd5b1-062cd5c0) Watcher2c's slot 0 (BaseIdea::SetContext) with
    // (OwnerContextPtr14, NavHelperPtr24, Param2); AL = the binder's result.
    bool const bOk = BindContext(Param1, Param2, Param3);
    if (bOk)
        Watcher2c.SetContext(OwnerContextPtr14, NavHelperPtr24, Param2);
    return bOk;
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 202772-202773
// (LAB_062cd5d0) -- a pure tail-call to the shared FUN_062d20d0 with `this`
// unchanged. See common_idlebehavior.h for why this specific call site's
// body is safe to reproduce despite decomp-tracker item 1533's global
// `simplified` status.
void IdleBehavior::ResetTickState()
{
    Behavior::Slot3();   // `JMP FUN_062d20d0` -- retail's slot 3 IS Behavior's body
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 202786-202797
// (FUN_062cd5e0), 8 instructions.
void IdleBehavior::Disengage()
{
    Watcher2c.Commit();     // 062cd5e9 `CALL [EAX+0x10]` on +0x2c (IdleIdea: bare RET)
    Behavior::Slot4();      // 062cd5ef `JMP FUN_062d20f0`
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 204100-204108
// (LAB_062ce3c0), 7 instructions: EAX = byte +0x29 ? 3 : 1; [+0xc] = EAX; RET 4.
int IdleBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    ModeSelector0xc = Mode;
    return Mode;
}

// Own vtable slot 7 (offset 0x1c). Common.dll.txt 202811-202833
// (FUN_062cd600), 18 instructions. Confirmed 1:1 against both raw disasm and
// Common.dll.c 103415-103425.
int IdleBehavior::Update()
{
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return UpdateResult10;
    }

    // 062cd614-062cd61e: Watcher2c slot 1 (IdleIdea::Update) with DeltaTime1c; the
    // result is discarded.
    Watcher2c.Update(DeltaTime1c);

    UpdateResult10 = 1;
    return UpdateResult10;
}

// Own vtable slot 10 (offset 0x28). Common.dll.txt 202839-202842
// (LAB_062cd630), 3 instructions -- ICF-shared with FormationBehavior's own
// slot 10 (BaseFollowerBehavior_DefaultRequestFlag, common_behavior.h/.cpp);
// see common_idlebehavior.h's class comment for why this is reproduced as
// its own method rather than a cast to that unrelated type.
bool IdleBehavior::RequestFlag()
{
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    return true;
}

