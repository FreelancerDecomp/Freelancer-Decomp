// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Mindist_Settings -- Common.dll .text:0634da50-0634db66 and the static
// initializer LAB_0634da30/LAB_0634da40 (see ivp_mindist_settings.h).
//
// x87: retail keeps intermediates on the FPU stack (53-bit precision under the
// VC6 runtime) and rounds only at each FST/FSTP float, so every chained value is
// carried in `double` here and cast to float exactly where retail stores one.

#include "ivp_mindist_settings.h"

#include <cmath>

// LAB_0634da30 -> LAB_0634da40 -> FUN_0634db40 at DLL load (.data:063ea60c).
FIVP_MindistSettingsStorage G_IVP_MindistSettingsStorage;
IVP_Mindist_Settings& G_IVP_MindistSettings = G_IVP_MindistSettingsStorage.Settings;

// FUN_0634db40: SetCollisionTolerance(0.01f) (PUSH 0x3c23d70a), SetField_0x130(1.0f)
// (PUSH 0x3f800000), then +0x12c = 0x3e8; returns this. +0x134 is left alone.
IVP_Mindist_Settings::IVP_Mindist_Settings()
{
    SetCollisionTolerance(0.00999999977648258f); // 0x3c23d70a
    SetField_0x130(1.0f);
    max_spawned_mindist_count = 0x3e8;
}

// FUN_0634db30: MOV EAX,[ESP+4]; MOV [ECX+0x130],EAX; RET 4.
void IVP_Mindist_Settings::SetField_0x130(float Value)
{
    Field_0x130 = Value;
}

// FUN_0634da50. Constants: DAT_063a5464 0.1f, DAT_063a53fc 0.9f, DAT_063a55a0
// 0.015625f, DAT_063a559c 0.3f, DAT_063a5598 19.62f, DAT_063a5404 0.01f,
// DAT_063a5594 2.5f, DAT_063a543c 20.0f.
void IVP_Mindist_Settings::SetCollisionTolerance(float D)
{
    const double Dd = D;

    // 0634da51-0634da7b: FLD D; FMUL 0.1f; FST [+0]; FLD D; FMUL 0.9f; FADDP;
    // FST [+4]; FSTP [+0x108].
    const double Tenth = Dd * (double)0.1f;
    real_coll_dist = (float)Tenth;
    const double MinDist = Dd * (double)0.9f + Tenth;
    min_coll_dists = (float)MinDist;
    max_coll_dist = (float)MinDist;

    // 0634da81-0634daa6: counter starts at 0x3f, EDX at +0x104; each pass stores
    // [EDX+4] = ([+0x108] - [+4]) * (k * 0.015625f) + [+4] with k = the counter
    // before the decrement, walking down to k = 0 at +0x008 (exit on JGE after
    // the counter reaches -1).
    for (int K = 0x3f; K >= 0; --K)
    {
        const double Diff = (double)max_coll_dist - (double)min_coll_dists;
        const double Step = (double)K * (double)0.015625f;
        CollDistRamp[K] = (float)(Diff * Step + (double)min_coll_dists);
    }

    // 0634daa8-0634dab2: FLD D; FADD [+0x108]; FST [+0x10c] -- Top stays on the stack.
    const double Top = Dd + (double)max_coll_dist;
    friction_dist = (float)Top;

    // 0634dab8-0634dad5: FLD D; FMUL 0.3f; FADD ST0,ST1; FST [+0x110];
    // FSUB [+4]; FMUL 19.62f; FSQRT; FSTP [+0x114].
    const double Above = Dd * (double)0.3f + Top;
    keeper_dist = (float)Above;
    speed_after_keeper_dist = (float)std::sqrt((Above - (double)min_coll_dists) * (double)19.62f);

    // 0634dadb-0634dae5
    distance_keepers_safety = (float)(Dd * (double)0.01f);
    // 0634daeb-0634daf7
    max_distance_for_friction = (float)(Dd * (double)2.5f + Top);
    // 0634dafd-0634db09; FSTP ST0 drops Top.
    max_distance_for_impact_system = (float)(Dd * (double)20.0f + Top);
    // 0634db11-0634db17: FLD D; FADD ST0,ST0.
    min_vertical_speed_at_collision = (float)(Dd + Dd);
    // 0634db1d-0634db26: FLD [+4]; FMUL 0.1f.
    mindist_change_force_dist = (float)((double)min_coll_dists * (double)0.1f);
}
