// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cruisebehavior.h"
#include "common_systemmanager.h"  // SystemManager::Thrust -- ThrustManager::engage_cruise

// CruiseBehavior -- see common_cruisebehavior.h for the full RTTI/layout
// writeup this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// ----------------------------------------------------------------------------
// FUN_062ce360 (Common.dll.txt 204036-204047) -- constructor, 9 instructions,
// the smallest in the whole TractorArm family so far. Calls the shared
// base-init helper FUN_062d2020(this) (it writes +0x04..+0x29, see Behavior in common_behavior.h) and installs the vtable; no field is
// zeroed, no sub-object is constructed. Every field is left to SetContext().
CruiseBehavior::CruiseBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)   // retail leaves this uninitialized until SetContext(); zeroed
                                 // here for well-defined C++ semantics (documented deviation).
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , Opaque18{}
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

// Vtable slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared
// verbatim across 7 unrelated vtables (decomp-tracker item 1320, already
// documented in common_idlebehavior.cpp) -- NO CruiseBehavior-specific field
// cleanup exists anywhere in it.
CruiseBehavior::~CruiseBehavior()
{
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 205851-205852
// (LAB_062cf3e0) -- a pure `JMP FUN_062d2090` tail-call, i.e. this method IS
// the shared Behavior-family context initializer with no CruiseBehavior-
// specific wrapper logic at all (see class comment for the cross-sibling ICF
// evidence this gives for 0x0639f650/0x0639feb8 too).
bool CruiseBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // LAB_062cf3e0: `JMP FUN_062d2090` -- Behavior::BindContext.
    return BindContext(Param1, Param2, Param3);
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 204058-204068
// (FUN_062ce380), 9 instructions.
void CruiseBehavior::Engage()
{
    Behavior::Slot3();                                               // 062ce383 FUN_062d20d0
    // 062ce388-062ce390: ThrustManager::engage_cruise(true) on NavHelperPtr24 + 0x18.
    NavHelperPtr24->Thrust.engage_cruise(true);
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 204081-204091
// (FUN_062ce3a0), 10 instructions, fully self-contained.
void CruiseBehavior::Disengage()
{
    // 062ce3a3-062ce3ab: ThrustManager::engage_cruise(false) on NavHelperPtr24 + 0x18.
    NavHelperPtr24->Thrust.engage_cruise(false);
    Behavior::Slot4();                                               // 062ce3b3 JMP FUN_062d20f0
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 204100-204108
// (LAB_062ce3c0), 7 instructions -- ICF-shared byte-identical body with
// IdleBehavior::SetModeFromFlags (common_idlebehavior.cpp); reproduced here
// as CruiseBehavior's own method rather than cast to that unrelated type.
int CruiseBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    ModeSelector0xc = Mode;
    return Mode;
}

// Own vtable slot 7 (offset 0x1c). Common.dll.txt 204118-204126
// (LAB_062ce3e0), 8 instructions, fully self-contained -- the simplest
// Update() anywhere in this family so far.
int CruiseBehavior::Update()
{
    UpdateResult10 = (OwnerContextPtr14 != nullptr) ? 1 : 3;
    return UpdateResult10;
}

