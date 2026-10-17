// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Two IVP_Compact_Ledge_Solver members (FUN_0635b940, FUN_0635b210); the rest
// of the class is in ivp_compact_ledge_solver.cpp.
#include "../ivp_physics/ivp_mindist.h"
#include "ivp_compact_ledge_solver.h"

// ============================================================================
// IVP_Compact_Ledge_Solver (ivp_compact_ledge_solver.h). Raw-ledge helpers traced against
// the listing with retail's x87 rounding points: values that stay on the FPU stack are
// double here, every FSTP dword is a float conversion.
// ============================================================================

// FUN_0635b940 (.text:0635b940-0635ba09), cdecl (edge, ledge, out).
// P0 = start(edge), P1 = start(next edge, DAT_063ee4e0), P2 = start(prev edge, DAT_063ee4f0).
// b = P2 - P0 is spilled as floats; a = P1 - P0 stays on the stack.
// out.x = ay*bz - az*by, out.y = az*bx - ax*bz, out.z = ax*by - ay*bx.
void IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(const IVP_Compact_Edge* Edge,
                                                                  const IVP_Compact_Ledge* Ledge,
                                                                  IVP_U_Point* OutNormal)
{
    const IVP_U_Float_Point* P0 = Ledge->GetPoint(Edge);
    const IVP_U_Float_Point* P1 = Ledge->GetPoint(Edge->GetNextEdge());
    const IVP_U_Float_Point* P2 = Ledge->GetPoint(Edge->GetPrevEdge());

    const float Bx = (float)((double)(P2->X) - P0->X);
    const float By = (float)((double)(P2->Y) - P0->Y);
    const float Bz = (float)((double)(P2->Z) - P0->Z);

    const double Ax = (double)(P1->X) - P0->X;
    const double Ay = (double)(P1->Y) - P0->Y;
    const double Az = (double)(P1->Z) - P0->Z;

    OutNormal->k[0] = (float)(Ay * Bz - Az * By);
    OutNormal->k[1] = (float)(Az * Bx - Ax * Bz);
    OutNormal->k[2] = (float)(Ax * By - Ay * Bx);
}

// FUN_0635b210 (.text:0635b210-0635b3ad), cdecl (ledge, edge, point, result).
// The three vertices come from the TRIANGLE (edge & ~0xf): T0 = start(tri+4),
// T1 = start(tri+8), T2 = start(tri+0xc). a = T0 - T1 (stack), b = T2 - T1 (floats),
// d = P - T1 (floats). AA = (az*az + ax*ax) + ay*ay, BB = (bz*bz + by*by) + bx*bx,
// AB = (bz*az + by*ay) + bx*ax, DA = (dz*az + dy*ay) + dx*ax, DB = (dz*bz + dy*by) + dx*bx,
// all float. Det = float(BB*AA - AB*AB). X = DA*BB - DB*AB (weight of T0),
// Y = DB*AA - DA*AB (weight of T2), W1 = (Det - X) - Y (weight of T1), all extended.
// Slots: byte table DAT_063ee500 = {00,08,04,00,08} indexed by k = ((edge >> 2) & 3) - 1:
// result[tbl[k]] = Y, result[tbl[k+1]] = W1, result[tbl[k+2]] = X; result[3] = Det.
void IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(const IVP_Compact_Ledge* Ledge,
                                                             const IVP_Compact_Edge* Edge,
                                                             const IVP_U_Point* Point,
                                                             IVP_Unscaled_QR_Result* OutResult)
{
    static const int SlotTable[5] = { 0, 2, 1, 0, 2 }; // DAT_063ee500 bytes / 4

    const IVP_Compact_Triangle* Triangle = Edge->GetCompactTriangle();
    const IVP_U_Float_Point* T0 = Ledge->GetPoint(Triangle->GetEdge(0));
    const IVP_U_Float_Point* T1 = Ledge->GetPoint(Triangle->GetEdge(1));
    const IVP_U_Float_Point* T2 = Ledge->GetPoint(Triangle->GetEdge(2));

    const double Ax = (double)(T0->X) - T1->X;
    const double Ay = (double)(T0->Y) - T1->Y;
    const double Az = (double)(T0->Z) - T1->Z;

    const float Bx = (float)((double)(T2->X) - T1->X);
    const float By = (float)((double)(T2->Y) - T1->Y);
    const float Bz = (float)((double)(T2->Z) - T1->Z);

    const float AA = (float)((Az * Az + Ax * Ax) + Ay * Ay);
    const float BB = (float)(((double)(Bz) * Bz + (double)(By) * By) +
                                        (double)(Bx) * Bx);

    const float Dx = (float)((double)(Point->k[0]) - T1->X);
    const float Dy = (float)((double)(Point->k[1]) - T1->Y);
    const float Dz = (float)((double)(Point->k[2]) - T1->Z);

    const float AB = (float)((Bz * Az + By * Ay) + Bx * Ax);
    const float Det = (float)((double)(BB) * AA - (double)(AB) * AB);
    const float DA = (float)((Dz * Az + Dy * Ay) + Dx * Ax);
    const float DB = (float)(((double)(Dz) * Bz + (double)(Dy) * By) +
                                        (double)(Dx) * Bx);

    const double X = (double)(DA) * BB - (double)(DB) * AB;
    const double Y = (double)(DB) * AA - (double)(DA) * AB;
    const double W1 = ((double)(Det) - X) - Y;

    const int K = static_cast<int>((reinterpret_cast<uintptr_t>(Edge) >> 2) & 3) - 1;
    float* Out = OutResult->Checks;
    OutResult->Det = Det;
    Out[SlotTable[K]] = (float)(Y);
    Out[SlotTable[K + 1]] = (float)(W1);
    Out[SlotTable[K + 2]] = (float)(X);
}
