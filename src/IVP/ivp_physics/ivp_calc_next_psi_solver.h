// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_core.h"
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_vector.h"

class IVP_Environment;
class IVP_Event_Sim;
class IVP_Hull_Manager;

// ============================================================================
// IVP_Calc_Next_PSI_Solver -- the per-core PSI integrator, Common.dll
// .text:06351310-0635212f. A one-member helper: every method's ECX points at a
// stack slot that holds the core (`MOV EDI,ECX; MOV ESI,[EDI]` at :06351317,
// `MOV EDI,[ECX]` at :06351707, `MOV EAX,[ECX]` at :06351a92, `MOV EAX,[ESI]`
// at :06351b98), and the two drivers build it by storing the core into their
// own argument slot and passing that slot's address as `this`
// (`MOV [ESP+param_1],ECX; LEA ECX,[ESP+param_1]` at :06351f1a-:06351f1f).
// The class and method names are the IVP SDK's; retail keeps no symbols here.
//
// IVP_Core field names used below are the port's (ivp_core.h); where they
// differ from the SDK roles these functions establish, the SDK role is given
// in the comment at the access:
//   +0x08 SpeedLimit          = max_surface_deviation
//   +0x10 AxisLock            = car_wheel
//   +0x4c SpinClipping        = spin_clipping (per-axis |rot_speed| bound)
//   +0x134 RotAxisOS          = rotation_axis_world_space
//   +0x144 Margin             = current_speed
//   +0x148 MaxSurfaceDeviation = abs_omega
//   +0x14c BoundingRadius     = max_surface_rot_speed
// ============================================================================
class IVP_Calc_Next_PSI_Solver
{
public:
    IVP_Core* core; // +0x00

    explicit IVP_Calc_Next_PSI_Solver(IVP_Core* Core)
        : core(Core)
    {
    }

    // FUN_06351310 (RET 8). Speed limits / anomaly callbacks, current speed,
    // the rotation step, the next-PSI position and orientation, and the
    // per-object hull manager update that collects the hull managers whose
    // nearest synapse is already crossed into ActiveHullManagers.
    void calc_next_PSI_matrix(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers);

    // FUN_06351700 (RET 8). Rotation of one PSI step into QCoreFCore (and the
    // torque-free precession of the core's rot_speed in the sub-stepped case).
    void calc_rotation_matrix(float DeltaTime, IVP_U_Quat* QCoreFCore);

    // FUN_06351a50 (RET 4). abs_omega, rotation axis and max_surface_rot_speed
    // from a step rotation quaternion (only x/y/z are read).
    void calc_psi_rotation_axis(const IVP_U_Quat* QCoreFCore);

    // FUN_06351b90 (RET 0xc). Teleport the core to Rotation / Position.
    void set_transformation(const IVP_U_Quat* Rotation, const IVP_U_Point* Position, int bOptimizeForRepeatedCalls);

    // FUN_06351eb0 (cdecl, plain RET). calc_next_PSI_matrix for every core in
    // CoresWhichNeedCalcNextPsi, last element first.
    static void commit_all_calc_next_PSI_matrix(IVP_Environment* Environment,
                                                IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi,
                                                IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers);

    // FUN_06351f80 (cdecl, plain RET). Fire the crossed hull synapses of every
    // collected hull manager, last element first, then rebase managers whose
    // reset time has passed. Environment is not read (retail never touches its
    // first argument).
    static void commit_all_hull_managers(IVP_Environment* Environment,
                                         IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers);
};
static_assert(sizeof(IVP_Calc_Next_PSI_Solver) == 0x04, "IVP_Calc_Next_PSI_Solver is one IVP_Core* slot");
