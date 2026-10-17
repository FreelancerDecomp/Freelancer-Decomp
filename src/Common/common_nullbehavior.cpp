// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_nullbehavior.h"

// NullBehavior -- see common_nullbehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// No dedicated ctor function exists in retail -- NullBehavior is only ever produced by the
// id-0 factory FUN_062d16d0 (see class comment): `operator new(0x2c)`, then
// `CALL FUN_062d2020` with the descriptor's own type id, then the vtable install that the
// factory does inline (`MOV [ESI],063a0244`, .text:062d170f) because there is no class-specific
// constructor to do it. This port's constructor stands in for that whole sequence.
//
// CORRECTED 2026-09-17 (claude:common-factory). The text that stood here called FUN_062d2020 a
// "documented no-op" and said "nothing observable differs since FUN_062d2020 touches no fields
// either way". Both halves are wrong: .text:062d2020-062d205a writes ten fields spanning
// +0x04..+0x29, two of them to nonzero constants. The class had no storage for any of them,
// which is what the field block added to the header this pass fixes.
NullBehavior::NullBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(2)
    , UpdateResult10(1)
    , OwnerContext(nullptr)
    , Opaque18{}
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
    // Every value above is FUN_062d2020's own: +0x0c = 2 (.text:062d2049), +0x10 = 1
    // (.text:062d2050), everything else zero. See the class's field block for why this
    // storage had to exist before the factory could safely make one of these.
}

// Vtable slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared
// verbatim across 7 unrelated vtables (decomp-tracker item 1320) -- NO
// NullBehavior-specific field cleanup exists anywhere in it.
NullBehavior::~NullBehavior()
{
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 219477-219478
// (LAB_062d76a0), 2 instructions -- unconditionally succeeds, touches
// nothing (see class comment for why this is notable versus every other
// sibling's own SetContext).
bool NullBehavior::SetContext(void* /*Param1*/, void* /*Param2*/, void* /*Param3*/)
{
    return true;
}

// Own vtable slot 7 (offset 0x1c). ICF-folded with the unrelated retail
// symbol Archetype::Solar::get_class_type (Common.dll.txt 209514), 2
// instructions: `MOV EAX,2; RET`.
int NullBehavior::Update()
{
    return 2;
}
