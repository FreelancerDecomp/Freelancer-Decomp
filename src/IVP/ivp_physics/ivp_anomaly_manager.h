// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h" // For Vector
#include <cstddef>
#include <cstdint>

class IVP_Core;
class IVP_Environment;
class IVP_Real_Object;
class IVP_Ledge_Mindist;

// ============================================================================
// D-ivp-anomaly pass (2026-09-17, lane claude:common-ivp). These two classes
// were the last thing standing between IVP_Impact_Solver::FinalizeImpact /
// CommitCoreVelocityImmediate (decomp-tracker items 3270 / 3266) and a full
// port: both reach into IVP_Environment sub-managers that this port carried as
// untyped `void*`, and both were previously (mis)described in those items'
// notes as "controller_manager". The raw listing disproves that -- see the
// per-class evidence below. Nothing here is guessed from the real IVP/Havok
// sources: every field and every vtable slot listed as identified is backed by
// a cited instruction in EXE\Disasm\Common.dll.txt.
//
// Ownership: both objects are allocated and constructed by the real
// IVP_Environment constructor (FUN_0634bf50) and stored at Environment+0x20
// (anomaly manager) / Environment+0x24 (anomaly limits) -- see
// Common.dll.txt:420106-420174, where each is `operator_new(size)` followed by
// `ctor(this, 1)` and a store into the Environment. The literal `1` is the
// ctor's only argument and lands in each object's +0x04 field, which both
// classes' own "environment will be deleted" slot then tests before deleting
// itself (Common.dll.txt:06360e90 / 06360f00) -- i.e. it is a
// "delete-me-with-the-environment" ownership flag, NOT an Environment back
// pointer.
// ============================================================================

// ----------------------------------------------------------------------------
// IVP_Anomaly_Limits -- retail ctor FUN_06360e30 (decomp-tracker item 3416),
// Common.dll.txt:454257-454264 (7 instructions, the whole class's writer):
//   [this+0x00] = PTR_LAB_063a577c   (its 2-slot vtable, see below)
//   [this+0x04] = param_1            (the `1` the Environment ctor passes)
//   [this+0x08] = 0x44fa0000         (== 2000.0f)
//   [this+0x10] = 0x3fc90fdb         (== 1.5707964f, pi/2)
//   [this+0x0c] = 0x00011170         (== 70000, an INTEGER -- stored between
//                                     the two floats, matching retail's own
//                                     instruction order exactly)
// Size 0x14 is confirmed independently by its allocation site
// (Common.dll.txt:420159, `PUSH 0x14; CALL operator_new`).
//
// Field roles are pinned by their CONSUMERS, not by name-matching:
//   +0x08 is multiplied by 0.99f (DAT_063a57a0) and used as the target length
//         when rescaling an over-fast core's linear velocity
//         (IVP_Anomaly_Manager slot 0, Common.dll.txt:06360f20-06360f69), and
//         is compared squared against |Core->Speed|^2 by
//         IVP_Impact_Solver::DoImpactSystem's tail
//         (Common.dll.txt:06357d81-06357dba)   -> a maximum linear velocity.
//   +0x0c is compared against IVP_Core::PendingImpactCount (a 16-bit counter)
//         by IVP_Impact_Solver::CommitCoreVelocityImmediate
//         (Common.dll.txt:0635730a-06357312)   -> a maximum collision count.
//   +0x10 is multiplied by IVP_Environment::inv_delta_time (+0xB0) to form a
//         per-second angular-velocity bound, both in
//         IVP_Anomaly_Manager slot 1 (Common.dll.txt:06360f87-06360f90) and in
//         DoImpactSystem's tail (Common.dll.txt:06357d34-06357d3e)
//                                              -> a maximum angular velocity
//                                                 PER PSI (hence the name).
//
// vtable PTR_LAB_063a577c (.rdata:063a577c-063a5783, exactly 2 slots -- the
// next .rdata dword at 063a5784 is the anomaly manager's own vtable pointer,
// so the byte extent is unambiguous):
//   slot 0 = LAB_06360e90 : `if (this->bDeleteOnEnvironmentDelete && this)
//                            this->vtable[1](1);` i.e. delete this. Called by
//                            the real ~IVP_Environment (Common.dll.txt:
//                            420833-ish, see ivp_environment.cpp) with the
//                            Environment* as its one stack argument, which
//                            retail's body ignores.
//   slot 1 = FUN_06360e60 : the MSVC scalar-deleting destructor (calls
//                            FUN_06360e80, a vtable-reinstall-only trivial
//                            destructor body, then free() when the flag bit is
//                            set).
// ----------------------------------------------------------------------------
class IVP_Anomaly_Limits
{
public:
    explicit IVP_Anomaly_Limits(int32_t bDeleteWithEnvironment);

    // vtable slot 0 (LAB_06360e90).
    virtual void EnvironmentWillBeDeleted(IVP_Environment* Env);
    // vtable slot 1 (FUN_06360e60 / FUN_06360e80).
    virtual ~IVP_Anomaly_Limits();

    int32_t bDeleteOnEnvironmentDelete; // +0x04: ctor argument, 1 for the Environment-owned singleton
    float   MaxVelocity;                // +0x08: 2000.0f
    int32_t MaxCollisionsPerPsi;        // +0x0C: 70000
    float   MaxAngularVelocityPerPsi;   // +0x10: 1.5707964f (pi/2)
};

static_assert(sizeof(IVP_Anomaly_Limits) == 0x14, "IVP_Anomaly_Limits size must be 0x14 (operator_new(0x14) at Common.dll.txt:420159)");
static_assert(offsetof(IVP_Anomaly_Limits, bDeleteOnEnvironmentDelete) == 0x04, "IVP_Anomaly_Limits+0x04");
static_assert(offsetof(IVP_Anomaly_Limits, MaxVelocity) == 0x08, "IVP_Anomaly_Limits+0x08");
static_assert(offsetof(IVP_Anomaly_Limits, MaxCollisionsPerPsi) == 0x0C, "IVP_Anomaly_Limits+0x0C");
static_assert(offsetof(IVP_Anomaly_Limits, MaxAngularVelocityPerPsi) == 0x10, "IVP_Anomaly_Limits+0x10");

// ----------------------------------------------------------------------------
// IVP_Anomaly_Manager -- retail ctor FUN_06360eb0 (decomp-tracker item 3420),
// Common.dll.txt:454337-454341 (4 instructions):
//   [this+0x00] = PTR_LAB_063a5784   (its 7-slot vtable, see below)
//   [this+0x04] = param_1            (the same `1` ownership flag)
// Size 0x08 is confirmed by its allocation site (Common.dll.txt:420138,
// `PUSH 0x8; CALL operator_new`).
//
// IMPORTANT CORRECTION carried into ivp_environment.h this pass: the object at
// Environment+0x20 -- the one this class describes -- was previously labelled
// `performance_counter` in this port, and items 3266/3270's notes additionally
// mislabelled the whole access chain as going through `controller_manager`
// (+0x0C). Neither survives the listing: every call site loads
// `Core->Environment(+0x0C)` and then indexes +0x20 for the receiver and +0x24
// for the IVP_Anomaly_Limits argument, and the receiver's slot-0 body rescales
// a velocity vector to the limits object's +0x08 bound. This class is an
// anomaly/limit-violation policy object.
//
// vtable PTR_LAB_063a5784 (.rdata:063a5784-063a579f, 7 slots -- the following
// dword at 063a57a0 is DAT_063a57a0, the 0.99f constant slot 0 itself reads,
// not an 8th entry):
//   slot 0 (+0x00) = LAB_06360f20 : MaxVelocityExceeded          -- PORTED
//   slot 1 (+0x04) = LAB_06360f70 : MaxAngularVelocityExceeded   -- PORTED
//   slot 2 (+0x08) = LAB_06361130 : inter_penetration            -- PORTED
//   slot 3 (+0x0C) = LAB_06361490 : MaxCollisionsExceededCheckFreezing -- PORTED
//   slot 4 (+0x10) = LAB_06360f00 : EnvironmentWillBeDeleted     -- PORTED
//   slot 5 (+0x14) = LAB_06361460 : unidentified, 7 insns        -- DEFERRED
//   slot 6 (+0x18) = FUN_06360ed0 : scalar deleting destructor   -- PORTED
// ----------------------------------------------------------------------------
class IVP_Anomaly_Manager
{
public:
    explicit IVP_Anomaly_Manager(int32_t bDeleteWithEnvironment);

    // ---- slot 0 (LAB_06360f20, Common.dll.txt:454414-454444, RET 0xc so
    // three stack arguments; `this` is never dereferenced by the base body).
    // Rescales *Speed to exactly Limits->MaxVelocity * 0.99f (DAT_063a57a0 ==
    // 0x3F7D70A4), i.e. it clamps an over-fast core back just inside the
    // limit. `Core` is unused by the base implementation but is part of the
    // slot's signature -- DoImpactSystem passes it
    // (Common.dll.txt:06357db3-06357dba).
    virtual void MaxVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* Speed);

    // ---- slot 1 (LAB_06360f70, Common.dll.txt:454449-454482, RET 0xc).
    // Same shape as slot 0, but the bound is
    // Core->Environment->inv_delta_time * Limits->MaxAngularVelocityPerPsi *
    // 0.9f (DAT_063a53fc == 0x3F666666) -- i.e. the per-PSI angular limit
    // converted to a per-second one. Unlike slot 0 this body DOES use `Core`
    // (it is where the Environment comes from).
    virtual void MaxAngularVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* RotSpeed);

    // ---- slot 2 (LAB_06361130, .text:06361130-0636145d, RET 0xc): the library's
    // inter_penetration(mindist, object0, object1). Its caller is
    // IVP_Ledge_Mindist::mindist_rescue_push (06352b10), which passes (this
    // mindist, synapse[0] object, synapse[1] object). When neither core is fixed
    // (core flags & 0xc), or the fixed side's synapse is not a vertex / edge / face,
    // both objects are pushed apart (solve_inter_penetration_simple); otherwise the
    // free object is pushed out along the face normal of the fixed object's ledge
    // that is farthest in front of it. Body in ivp_environment.cpp (cmn-physys4).
    virtual void inter_penetration(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // FUN_06360fd0 (.text:06360fd0-06361125, thiscall, RET 8): pushes the two
    // objects apart along the line between their cores. Non-virtual; its only
    // callers are inter_penetration's two early exits (06361184 / 063611b5).
    // Library name solve_inter_penetration_simple.
    void solve_inter_penetration_simple(IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // ---- slot 3 (LAB_06361490, Common.dll.txt:454874-454876, RET 0x8).
    // The base body is literally `MOV EAX,1; RET 8` -- it always allows the
    // freeze. This is the slot IVP_Impact_Solver::CommitCoreVelocityImmediate
    // dispatches through when a core's PendingImpactCount exceeds
    // Limits->MaxCollisionsPerPsi (Common.dll.txt:0635731d).
    virtual int32_t MaxCollisionsExceededCheckFreezing(IVP_Anomaly_Limits* Limits, IVP_Core* Core);

    // ---- slot 4 (LAB_06360f00, Common.dll.txt:454392-454402, RET 0x4).
    // Identical body to IVP_Anomaly_Limits::EnvironmentWillBeDeleted, just
    // dispatching its own class's destructor slot (+0x18 instead of +0x04).
    // Called by the real ~IVP_Environment.
    virtual void EnvironmentWillBeDeleted(IVP_Environment* Env);

    // ---- slot 5 (LAB_06361460, .text:06361460-06361482, RET 0x8).
    // 2026-10-08 (cmn-phy5): the arguments are the two objects of a pair --
    // its only caller is slot 2 (06361130, :063613e7-:063613f7) passing its own
    // 2nd/3rd arguments, and `Param1->+0x18` is IVP_Real_Object::environment,
    // whose +0xac is delta_time and +0xb4 the gravity vector. Library name:
    // get_push_speed_penetration. Returns 2 * |gravity| * delta_time on the x87
    // stack.
    // Returned as double: the value stays on the x87 stack, 06360fd0 rounds it with
    // FSTP float (06361048) while 06361130 multiplies the unrounded value (06361412..).
    virtual double get_push_speed_penetration(IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // ---- slot 6 (FUN_06360ed0 / FUN_06360ef0).
    virtual ~IVP_Anomaly_Manager();

    int32_t bDeleteOnEnvironmentDelete; // +0x04: ctor argument, 1 for the Environment-owned singleton
};

static_assert(sizeof(IVP_Anomaly_Manager) == 0x08, "IVP_Anomaly_Manager size must be 0x8 (operator_new(0x8) at Common.dll.txt:420138)");
static_assert(offsetof(IVP_Anomaly_Manager, bDeleteOnEnvironmentDelete) == 0x04, "IVP_Anomaly_Manager+0x04");
