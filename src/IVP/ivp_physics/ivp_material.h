// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================================
// IVP materials (2026-10-08, claude-code:cmn-phy3).
//
// Until this date this header declared a class called IVP_Material that was
// really a second transcription of the 0x58-byte real-object construction
// template (ctor FUN_063486c0, name setters FUN_06348690/06348720, dtor
// FUN_06348670). Those four functions now live on the single declaration of
// that struct, IVP_RealObjectConstructionTemplate (ivp_real_object.h), and the
// name IVP_Material is given to the class that really is the material: the
// object IVP_Real_Object::material (+0x8c) points at.
//
// Retail builds the IVP sources without RTTI, so the class names below are the
// IVP SDK's; each class is identified by its vtable and constructor:
//
//   IVP_Material          abstract base, vtable .rdata:063a5534 (five
//                         _purecall slots + deleting destructor 0634bdf0),
//                         stamped by its destructor FUN_0634bde0.
//   IVP_Material_Simple   0x1c bytes, ctor FUN_0634bec0, vtable .rdata:063a554c.
//                         PhySys::Startup creates the game's only material
//                         (operator_new(0x1c) at .text:06289539, ctor with
//                         PhySys::MATERIAL_FRICTION / MATERIAL_ELASTICITY at
//                         .text:06289550-06289568, stored in DAT_063fc0bc), and
//                         the material manager's default is another one
//                         (0.5f, 0.5f).
//   IVP_Material_Manager  8 bytes, ctor FUN_0634bd80, vtable .rdata:063a551c;
//                         the environment owns one at +0x9c (the field the
//                         port long called phantom_listener).
// ============================================================================

class IVP_Environment;
class IVP_Impact_Solver_Long_Term;
struct IVP_U_Point; // ivp_math.h

// Base. The slot meanings are fixed by IVP_Material_Simple's bodies and by
// PhySys::Startup's arguments: slot 0 returns the ctor's first float
// (MATERIAL_FRICTION), slot 2 its second (MATERIAL_ELASTICITY); slots 1 and 3
// return fields the ctor zeroes, combined multiplicatively (slot 1, by
// ClampTangentDirection FUN_06353b50) and additively (slot 3, by the manager's
// slot 3). The names are the SDK's.
class IVP_Material
{
public:
    // +0x04: written by no constructor in the listing (FUN_0634bec0 leaves it
    // as operator_new garbage) and read by no traced function. TODO.
    uint32_t Field_0x04;

    // +0x08: tested as a dword by FUN_063562d0 (pair record SolverMode) and
    // FUN_06353b50 (`MOV EAX,[EDX+0x8]; TEST EAX,EAX`, .text:06353b7d-06353b84);
    // zeroed by FUN_0634bec0 (.text:0634becf). SDK name second_friction_x_enabled.
    int32_t SecondFrictionXEnabled;

    virtual float get_friction_factor() = 0;        // slot 0
    virtual float get_second_friction_factor() = 0; // slot 1
    virtual float get_elasticity() = 0;             // slot 2
    virtual float get_adhesion() = 0;               // slot 3
    virtual const char* get_name() = 0;             // slot 4
    // slot 5: FUN_0634bdf0 (deleting) -> FUN_0634bde0 (stamp 063a5534).
    virtual ~IVP_Material() {}
};

class IVP_Material_Simple : public IVP_Material
{
public:
    float FrictionFactor;       // +0x0c  slot 0 (06261db0 `FLD [ECX+0xc]`)
    float SecondFrictionFactor; // +0x10  slot 1 (06263ba0 `FLD [ECX+0x10]`)
    float Elasticity;           // +0x14  slot 2 (06332f60 `FLD [ECX+0x14]`)
    float Adhesion;             // +0x18  slot 3 (0634beb0 `FLD [ECX+0x18]`)

    // FUN_0634bec0 (thiscall, RET 8).
    IVP_Material_Simple(float FrictionValue, float ElasticityValue);
    // FUN_0634bef0 (deleting) -> FUN_0634bf10 (stamp 063a554c, then the base's).
    virtual ~IVP_Material_Simple() {}

    virtual float get_friction_factor() override { return FrictionFactor; }
    virtual float get_second_friction_factor() override { return SecondFrictionFactor; }
    virtual float get_elasticity() override { return Elasticity; }
    virtual float get_adhesion() override { return Adhesion; }
    // LAB_0634bf20: `MOV EAX,s_Simple_material_063edca8; RET`.
    virtual const char* get_name() override { return "Simple material"; }
};
static_assert(sizeof(IVP_Material_Simple) == 0x1c, "IVP_Material_Simple is operator_new(0x1c)");
static_assert(offsetof(IVP_Material_Simple, SecondFrictionXEnabled) == 0x08, "IVP_Material+0x08");
static_assert(offsetof(IVP_Material_Simple, FrictionFactor) == 0x0c, "IVP_Material_Simple+0x0c");
static_assert(offsetof(IVP_Material_Simple, Elasticity) == 0x14, "IVP_Material_Simple+0x14");
static_assert(offsetof(IVP_Material_Simple, Adhesion) == 0x18, "IVP_Material_Simple+0x18");

// The pair-interaction side. Slots 1-3 take the contact (the IVP_Impact_Solver_Long_Term a
// friction dist owns, whose +0x40/+0x44 hold the two sides' materials) and
// combine the two materials' values.
class IVP_Material_Manager
{
public:
    // +0x04: the ctor argument; slot 5 deletes the manager when it is set.
    int32_t DeleteOnEnvDelete;

    // FUN_0634bd80 (thiscall, RET 4): vtable 063a551c, +4 = argument.
    explicit IVP_Material_Manager(int32_t DeleteOnEnvDeleteIn)
        : DeleteOnEnvDelete(DeleteOnEnvDeleteIn)
    {
    }

    // slot 0, FUN_0634be10 (RET 8): both arguments ignored; returns one
    // process-wide IVP_Material_Simple(0.5f, 0.5f), created on first use.
    virtual IVP_Material* get_material_by_index(const IVP_U_Point* WorldPosition, int Index);
    // slot 1, LAB_0634bcf0 (RET 4): Materials[0]->slot0 * Materials[1]->slot0.
    virtual float get_friction_factor(IVP_Impact_Solver_Long_Term* Contact);
    // slot 2, LAB_0634bd20 (RET 4): Materials[0]->slot2 * Materials[1]->slot2.
    virtual float get_elasticity(IVP_Impact_Solver_Long_Term* Contact);
    // slot 3, LAB_0634bd50 (RET 4): Materials[0]->slot3 + Materials[1]->slot3.
    virtual float get_adhesion(IVP_Impact_Solver_Long_Term* Contact);
    // slot 4, LAB_0634bdc0: scalar deleting destructor (stamps 063a551c only).
    virtual ~IVP_Material_Manager() {}
    // slot 5, LAB_0634bda0 (RET 4): `if (+4) delete this`.
    virtual void environment_will_be_deleted(IVP_Environment* Environment);
};
static_assert(sizeof(IVP_Material_Manager) == 0x8, "IVP_Material_Manager is operator_new(8)");
