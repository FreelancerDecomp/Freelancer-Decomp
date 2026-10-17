// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_material.h"
#include "ivp_mindist.h"

// ============================================================================
// FUN_0634bec0 (thiscall, RET 8). Retail's store order is +0xc, +0x8, vtable,
// +0x10, +0x14, +0x18 (.text:0634bec8-0634bede); +0x04 is not written. The
// vtable store is the compiler's, so the member order below is the observable
// one: SecondFrictionXEnabled = 0, FrictionFactor = arg1, the two zeroed floats
// and Elasticity = arg2.
// ============================================================================
IVP_Material_Simple::IVP_Material_Simple(float FrictionValue, float ElasticityValue)
{
    FrictionFactor = FrictionValue;
    SecondFrictionXEnabled = 0;
    SecondFrictionFactor = 0.0f;
    Elasticity = ElasticityValue;
    Adhesion = 0.0f;
}

// ============================================================================
// FUN_0634be10, IVP_Material_Manager slot 0 (RET 8, both arguments unread).
// A function-local static guarded by bit 0 of DAT_0658a9f4: on first use
// operator_new(0x1c), FUN_0634bec0(0.5f, 0.5f) (`PUSH 0x3f000000` twice,
// .text:0634be54-0634be60), pointer stored in DAT_0658a9f0 (null if
// operator_new returned null, .text:0634be7b-0634be7d). No atexit is
// registered, so the material is never freed -- a pointer static reproduces
// that exactly.
// ============================================================================
IVP_Material* IVP_Material_Manager::get_material_by_index(const IVP_U_Point* WorldPosition, int Index)
{
    (void)WorldPosition;
    (void)Index;
    static IVP_Material_Simple* DefaultMaterial = new IVP_Material_Simple(0.5f, 0.5f);
    return DefaultMaterial;
}

// ============================================================================
// LAB_0634bcf0 (RET 4). Contact+0x44 (the second side) is asked first and its
// result spilled to a float (`FSTP float ptr [ESP+0x8]`, .text:0634bcff), then
// Contact+0x40's slot 0 is multiplied by that float on the FPU.
// ============================================================================
float IVP_Material_Manager::get_friction_factor(IVP_Impact_Solver_Long_Term* Contact)
{
    const float SecondSide = Contact->Materials[1]->get_friction_factor();
    return Contact->Materials[0]->get_friction_factor() * SecondSide;
}

// ============================================================================
// LAB_0634bd20 (RET 4). Same shape as slot 1 but slot 2 of each material, and
// the first side (+0x40) is called first (.text:0634bd24-0634bd38).
// ============================================================================
float IVP_Material_Manager::get_elasticity(IVP_Impact_Solver_Long_Term* Contact)
{
    const float FirstSide = Contact->Materials[0]->get_elasticity();
    return Contact->Materials[1]->get_elasticity() * FirstSide;
}

// ============================================================================
// LAB_0634bd50 (RET 4). Slot 3 of each material, first side first, summed
// (`FADD float ptr [ESP+0x8]`, .text:0634bd6b).
// ============================================================================
float IVP_Material_Manager::get_adhesion(IVP_Impact_Solver_Long_Term* Contact)
{
    const float FirstSide = Contact->Materials[0]->get_adhesion();
    return Contact->Materials[1]->get_adhesion() + FirstSide;
}

// ============================================================================
// LAB_0634bda0 (RET 4). `MOV EAX,[ECX+4]; TEST; JZ; TEST ECX,ECX; JZ;
// CALL [vtable+0x10](1)` -- the scalar deleting destructor, slot 4. The
// `this != null` test is the compiler's delete-expression check.
// ============================================================================
void IVP_Material_Manager::environment_will_be_deleted(IVP_Environment* Environment)
{
    (void)Environment;
    if (DeleteOnEnvDelete != 0)
    {
        delete this;
    }
}
