// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Compact_Ledge_Solver (ivp_compact_ledge_solver.h): raw-ledge distance helpers and the
// ledge-tree collector. Traced against EXE\Disasm\Common.dll.txt with retail's x87 rounding
// points: values kept on the FPU stack are double, every `FSTP float ptr` is a float.

#include "ivp_compact_ledge_solver.h"

#include <cstring>

#include "ivp_compact_surface.h"
#include "ivp_mindist_minimize.h"          // IVP_Cache_Ledge_Point
#include "../ivp_physics/ivp_real_object.h" // IVP_NarrowPhaseCacheSlot (the cache object)
#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (FUN_0635d3e0)
#include "../ivp_utility/ivp_core_trap.h"   // IVP_CoreTrap: retail's `MOV dword ptr [0],0`

namespace
{
    uint32_t IVP_SolverFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // The sample parameters of calc_unscaled_KK_vals' near-parallel branch, stored to the stack
    // at :0635b6ea-:0635b753 (0xbf800000, 0x3f000000, ...). The K samples use the first 11, the
    // L samples the first 9.
    const float GKKSampleParams[13] = { -1.0f, 0.5f, 2.0f, 0.0f, 1.0f, -0.001f, 0.001f, 0.999f, 1.001f,
                                        -1.0e-6f, 1.0e-6f, 0.999999f, 1.000001f };
}

// FUN_0635b150 (.text:0635b150-0635b207), cdecl (ledge, edge, point, result).
// P0 = start(edge), P1 = start(next edge). e = P1 - P0 and a = P0 - p stay on the stack,
// b = P1 - p is spilled as floats.
//   Checks[0] = float(-((ez*az + ey*ay) + ax*ex)), Checks[1] = float((ez*bz + ey*by) + bx*ex).
void IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(const IVP_Compact_Ledge* Ledge,
                                                           const IVP_Compact_Edge* Edge,
                                                           const IVP_U_Point* Point,
                                                           IVP_Unscaled_S_Result* OutResult)
{
    const IVP_U_Float_Point* P0 = Ledge->GetPoint(Edge);
    const IVP_U_Float_Point* P1 = Ledge->GetPoint(Edge->GetNextEdge());

    const double Ex = (double)(P1->X) - P0->X;
    const double Ey = (double)(P1->Y) - P0->Y;
    const double Ez = (double)(P1->Z) - P0->Z;

    const double Ax = (double)(P0->X) - Point->k[0];
    const double Ay = (double)(P0->Y) - Point->k[1];
    const double Az = (double)(P0->Z) - Point->k[2];

    const float Bx = (float)((double)(P1->X) - Point->k[0]);
    const float By = (float)((double)(P1->Y) - Point->k[1]);
    const float Bz = (float)((double)(P1->Z) - Point->k[2]);

    OutResult->Checks[0] = (float)(-((Ez * Az + Ey * Ay) + Ax * Ex));
    OutResult->Checks[1] = (float)((Ez * Bz + Ey * By) + Bx * Ex);
}

// FUN_0635ba10 (.text:0635ba10-0635bb31), cdecl (ledge, edge, point).
// e = float(P1 - P0); d = float(p - P0) for x/y, dz kept on the stack;
// c = d x e: cx = float(dy*ez - dz*ey), cy = dz*ex - dx*ez (stack), cz = float(dx*ey - dy*ex);
// f = float(P0 - P1); L = (fz*fz + fx*fx) + fy*fy;
// result = ((cz*cz + cy*cy) + cx*cx) * (1 / L).
double IVP_Compact_Ledge_Solver::quad_dist_edge_to_point_K_space(const IVP_Compact_Ledge* Ledge,
                                                                 const IVP_Compact_Edge* Edge,
                                                                 const IVP_U_Point* Point)
{
    const IVP_U_Float_Point* P0 = Ledge->GetPoint(Edge);
    const IVP_U_Float_Point* P1 = Ledge->GetPoint(Edge->GetNextEdge());

    const float Ex = (float)((double)(P1->X) - P0->X);
    const float Ey = (float)((double)(P1->Y) - P0->Y);
    const float Ez = (float)((double)(P1->Z) - P0->Z);

    const float Dx = (float)((double)(Point->k[0]) - P0->X);
    const float Dy = (float)((double)(Point->k[1]) - P0->Y);
    const double Dz = (double)(Point->k[2]) - P0->Z;

    const float Cx = (float)((double)(Dy) * Ez - Dz * Ey);
    const double Cy = Dz * Ex - (double)(Dx) * Ez;
    const float Cz = (float)((double)(Dx) * Ey - (double)(Dy) * Ex);

    const float Fx = (float)((double)(P0->X) - P1->X);
    const float Fy = (float)((double)(P0->Y) - P1->Y);
    const float Fz = (float)((double)(P0->Z) - P1->Z);

    const double QuadLength = ((double)(Fz) * Fz + (double)(Fx) * Fx) +
                              (double)(Fy) * Fy;
    const double InvQuadLength = 1.0 / QuadLength; // FDIVR DAT_063a53d0
    const double QuadCross = ((double)(Cz) * Cz + Cy * Cy) + (double)(Cx) * Cx;
    return QuadCross * InvQuadLength;
}

// FUN_0635bb40 (.text:0635bb40-0635bcb5), cdecl (ledge, triangle, point).
// Inside the triangle (no sign bit in the three QR checks, -0.0 counts as negative):
//   h = ((ny*py + nz*pz) + nx*px) - ((ny*Py + nz*Pz) + nx*Px) with P = start(edge 0),
//   N = (nz*nz + ny*ny) + nx*nx, result = h * ((1 / N) * h).
// Otherwise the minimum over edges 2, 1, 0 (starting from the float 1e20f) of:
//   s[0] < 0 (or unordered)  -> |p - start(edge)|^2 = (dz*dz + dy*dy) + dx*dx;
//   else s[1] < 0 (or unord.) -> edge skipped;
//   else quad_dist_edge_to_point_K_space. FCOM best; TEST AH,0x1; JZ -> kept only when
//   strictly smaller; stored as float.
double IVP_Compact_Ledge_Solver::calc_qlen_PF_F_space(const IVP_Compact_Ledge* Ledge,
                                                      const IVP_Compact_Triangle* Triangle,
                                                      const IVP_U_Point* Point)
{
    const IVP_Compact_Edge* Edge0 = Triangle->GetFirstEdge();
    IVP_Unscaled_QR_Result QR;
    calc_unscaled_qr_vals_F_space(Ledge, Edge0, Point, &QR);

    if (((IVP_SolverFloatBits(QR.Checks[1]) | IVP_SolverFloatBits(QR.Checks[2]) |
          IVP_SolverFloatBits(QR.Checks[0])) & 0x80000000u) == 0)
    {
        const IVP_U_Float_Point* P = Ledge->GetPoint(Edge0);
        IVP_U_Point Normal;
        calc_hesse_vec_object_not_normized(Edge0, Ledge, &Normal);
        const double Nx = Normal.k[0], Ny = Normal.k[1], Nz = Normal.k[2];

        const double PointDist = (Ny * Point->k[1] + Nz * Point->k[2]) + Nx * Point->k[0];
        const double PlaneDist = (Ny * P->Y + Nz * P->Z) + Nx * P->X;
        const double Height = PointDist - PlaneDist;
        const double QuadNormal = (Nz * Nz + Ny * Ny) + Nx * Nx;
        return Height * ((1.0 / QuadNormal) * Height);
    }

    float Best = 1e20f; // 0x60ad78ec
    const IVP_Compact_Edge* Edge = Triangle->GetEdge(2);
    for (int Count = 3; Count != 0; --Count, --Edge)
    {
        IVP_Unscaled_S_Result S;
        calc_unscaled_s_val_K_space(Ledge, Edge, Point, &S);

        double Dist;
        if (!(S.Checks[0] >= 0.0f))
        {
            const IVP_U_Float_Point* P = Ledge->GetPoint(Edge);
            const double Dx = (double)(Point->k[0]) - P->X;
            const double Dy = (double)(Point->k[1]) - P->Y;
            const double Dz = (double)(Point->k[2]) - P->Z;
            Dist = (Dz * Dz + Dy * Dy) + Dx * Dx;
        }
        else if (!(S.Checks[1] >= 0.0f))
        {
            continue;
        }
        else
        {
            Dist = quad_dist_edge_to_point_K_space(Ledge, Edge, Point);
        }

        if (!(Dist >= Best)) // unordered also stores (C0 set)
            Best = (float)(Dist);
    }
    return Best;
}

// FUN_0635aba0 (.text:0635aba0-0635abea): while the node has a right son, recurse into the
// left son (node + 1) and continue with the right one; then append the terminal node's
// ledge, growing the big vector through FUN_0635d3e0 (IVP_U_BigVector_Grow) when
// Count >= Capacity.
void IVP_Compact_Ledge_Solver::get_all_ledges(const IVP_Compact_Ledgetree_Node* Node,
                                              IVP_SurfaceManager_OutList* OutLedges)
{
    while (Node->offset_right_node != 0)
    {
        get_all_ledges(Node->left_son(), OutLedges);
        Node = Node->right_son();
    }

    const IVP_Compact_Ledge* Ledge = Node->get_compact_ledge();
    if (!(OutLedges->Count < OutLedges->Capacity))
        IVP_U_BigVector_Grow(OutLedges); // FUN_0635d3e0
    OutLedges->Elems[OutLedges->Count] = const_cast<IVP_Compact_Ledge*>(Ledge);
    OutLedges->Count = OutLedges->Count + 1;
}

// FUN_0635abf0 (.text:0635abf0-0635ac07).
void IVP_Compact_Ledge_Solver::get_all_ledges(const IVP_Compact_Surface* Surface,
                                              IVP_SurfaceManager_OutList* OutLedges)
{
    get_all_ledges(Surface->get_compact_ledge_tree_root(), OutLedges);
}

// ---- cmn-ivp2c block (2026-10-08): helpers of IVP_Mindist_Minimize_Solver ----

// FUN_0635ae60 (.text:0635ae60-0635af68), cdecl (P, CacheP, CacheOther, out).
// p = CacheP->compact_poly_points[P & 0xffff]; M = CacheP->cache_object + 0x1c,
// O = CacheOther->cache_object + 0x1c (IVP_U_Matrix: rows at +0/+0x10/+0x20, vv at +0x30).
//   :0635ae7e-:0635aeb0 A = float(m00*px), B = float(m10*px) (spilled to the argument
//     slots), px*m20 stays on the stack; A = float(m01*py + A), B = float(m11*py + B),
//     acc += m21*py (FADDP ST2).
//   :0635aebd-:0635aef5 W.x = float((m02*pz + A) + vv.x), W.y = float((m12*pz + B) + vv.y),
//     W.z = (acc + m22*pz) + vv.z kept on the stack.
//   :0635aefa-:0635af10 d.x = float(W.x - O.vv.x), d.y = float(W.y - O.vv.y),
//     d.z = W.z - O.vv.z (stack).
//   :0635af13-:0635af60 out.i = float((O.r2[i]*dz + O.r1[i]*dy) + O.r0[i]*dx).
void IVP_Compact_Ledge_Solver::calc_pos_other_space(const IVP_Compact_Edge* P, const IVP_Cache_Ledge_Point* CacheP,
                                                    const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutPos)
{
    const IVP_U_Float_Point* const Point = CacheP->compact_poly_points + P->GetStartPointIndex();
    const IVP_U_Matrix& M = CacheP->cache_object->WorldTransform;

    const float Ax0 = (float)((double)(M.rows[0].k[0]) * Point->X);
    const float Ay0 = (float)((double)(M.rows[1].k[0]) * Point->X);
    const double Az0 = (double)(Point->X) * M.rows[2].k[0];
    const float Ax1 = (float)((double)(M.rows[0].k[1]) * Point->Y + Ax0);
    const float Ay1 = (float)((double)(M.rows[1].k[1]) * Point->Y + Ay0);
    const double Az1 = Az0 + (double)(M.rows[2].k[1]) * Point->Y;
    const float Wx = (float)(((double)(M.rows[0].k[2]) * Point->Z + Ax1) + M.vv.k[0]);
    const float Wy = (float)(((double)(M.rows[1].k[2]) * Point->Z + Ay1) + M.vv.k[1]);
    const double Wz = (Az1 + (double)(M.rows[2].k[2]) * Point->Z) + M.vv.k[2];

    const IVP_U_Matrix& O = CacheOther->cache_object->WorldTransform;
    const float Dx = (float)((double)(Wx) - O.vv.k[0]);
    const float Dy = (float)((double)(Wy) - O.vv.k[1]);
    const double Dz = Wz - O.vv.k[2];

    OutPos->k[0] = (float)(((double)(O.rows[2].k[0]) * Dz + (double)(O.rows[1].k[0]) * Dy) +
                                      (double)(O.rows[0].k[0]) * Dx);
    OutPos->k[1] = (float)(((double)(O.rows[2].k[1]) * Dz + (double)(O.rows[1].k[1]) * Dy) +
                                      (double)(O.rows[0].k[1]) * Dx);
    OutPos->k[2] = (float)(((double)(O.rows[2].k[2]) * Dz + (double)(O.rows[1].k[2]) * Dy) +
                                      (double)(O.rows[0].k[2]) * Dx);
}

// FUN_0635bcc0 (.text:0635bcc0-0635bd02), cdecl (ledge, edge, point). V = start(edge)
// (ledge + [ledge] + (edge & 0xffff) * 16); dx/dy/dz = point - V on the stack;
// returns (dz*dz + dy*dy) + dx*dx.
double IVP_Compact_Ledge_Solver::quad_dist_edge_start_to_point(const IVP_Compact_Ledge* Ledge,
                                                               const IVP_Compact_Edge* Edge, const IVP_U_Point* Point)
{
    const IVP_U_Float_Point* const Vertex = Ledge->GetPoint(Edge);
    const double Dx = (double)(Point->k[0]) - Vertex->X;
    const double Dy = (double)(Point->k[1]) - Vertex->Y;
    const double Dz = (double)(Point->k[2]) - Vertex->Z;
    return (Dz * Dz + Dy * Dy) + Dx * Dx;
}

// FUN_0635bd10 (.text:0635bd10-0635bd94), cdecl (point, ledge, edge).
//   :0635bd2a FUN_0635b150(ledge, edge, point, &s).
//   :0635bd2f-:0635bd40 no sign bit in (s[1] | s[0]) -> FUN_0635ba10(ledge, edge, point).
//   :0635bd54-:0635bd64 FCOMP s[0], 0.0f; TEST AH,0x1; JZ -> s[0] < 0 (or unordered)
//     takes FUN_0635bcc0(ledge, edge, point), otherwise FUN_0635bcc0(ledge, next(edge), point).
double IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(const IVP_U_Point* Point, const IVP_Compact_Ledge* Ledge,
                                                      const IVP_Compact_Edge* Edge)
{
    IVP_Unscaled_S_Result S;
    calc_unscaled_s_val_K_space(Ledge, Edge, Point, &S);
    if (((IVP_SolverFloatBits(S.Checks[1]) | IVP_SolverFloatBits(S.Checks[0])) & 0x80000000u) == 0)
        return quad_dist_edge_to_point_K_space(Ledge, Edge, Point);
    if (!(S.Checks[0] >= 0.0f))
        return quad_dist_edge_start_to_point(Ledge, Edge, Point);
    return quad_dist_edge_start_to_point(Ledge, Edge->GetNextEdge(), Point);
}

// ---- cmn-ivp2d block (2026-10-08) ----

// FUN_0635bda0 (.text:0635bda0-0635bdc5), cdecl (edge, cache, out): the point
// cache->compact_poly_points[edge & 0xffff] through FUN_0634d8d0 with ECX = cache->cache_object.
void IVP_Compact_Ledge_Solver::give_world_coords_AT(const IVP_Compact_Edge* Edge, const IVP_Cache_Ledge_Point* Cache,
                                                    IVP_U_Point* OutWorld)
{
    const IVP_U_Float_Point* const Point = Cache->compact_poly_points + Edge->GetStartPointIndex();
    IVP_TransformSlotToWorldSpace(Cache->cache_object, reinterpret_cast<const IVP_U_Point*>(Point), OutWorld);
}

// FUN_0635af70 (.text:0635af70-0635b02c), cdecl (vec, cacheIn, cacheOther, out).
// M = cacheIn->cache_object + 0x1c, O = cacheOther->cache_object + 0x1c.
//   :0635af7b-:0635afc1 w[i] = (M.r[i][2]*vz + M.r[i][1]*vy) + M.r[i][0]*vx, all three on the stack.
//   :0635afc3-:0635b025 out.x = (O.r2[0]*w2 + O.r1[0]*w1) + O.r0[0]*w0, likewise y and z
//     (column i of O), each rounded only at its FSTP.
void IVP_Compact_Ledge_Solver::transform_vec_other_space(const IVP_U_Point* Vec, const IVP_Cache_Ledge_Point* CacheIn,
                                                         const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutVec)
{
    const IVP_U_Matrix& M = CacheIn->cache_object->WorldTransform;
    const double Vx = Vec->k[0];
    const double Vy = Vec->k[1];
    const double Vz = Vec->k[2];
    const double W0 = (M.rows[0].k[2] * Vz + M.rows[0].k[1] * Vy) + M.rows[0].k[0] * Vx;
    const double W1 = (M.rows[1].k[2] * Vz + M.rows[1].k[1] * Vy) + M.rows[1].k[0] * Vx;
    const double W2 = (M.rows[2].k[2] * Vz + M.rows[2].k[1] * Vy) + M.rows[2].k[0] * Vx;

    const IVP_U_Matrix& O = CacheOther->cache_object->WorldTransform;
    OutVec->k[0] = (float)((O.rows[2].k[0] * W2 + O.rows[1].k[0] * W1) + O.rows[0].k[0] * W0);
    OutVec->k[1] = (float)((W2 * O.rows[2].k[1] + O.rows[1].k[1] * W1) + O.rows[0].k[1] * W0);
    OutVec->k[2] = (float)((W2 * O.rows[2].k[2] + O.rows[1].k[2] * W1) + O.rows[0].k[2] * W0);
}

// FUN_0635b030 (.text:0635b030-0635b144), cdecl (pos, cacheIn, cacheOther, out). Same rounding
// points as FUN_0635ae60 (calc_pos_other_space above), on the point passed in:
//   A = float(m00*px), B = float(m10*px), acc = px*m20; A = float(m01*py + A),
//   B = float(m11*py + B), acc += m21*py; W.x = float((m02*pz + A) + vv.x),
//   W.y = float((m12*pz + B) + vv.y), W.z = (acc + m22*pz) + vv.z (stack);
//   d.x = float(W.x - O.vv.x), d.y = float(W.y - O.vv.y), d.z = W.z - O.vv.z (stack);
//   out.i = float((O.r2[i]*dz + O.r1[i]*dy) + O.r0[i]*dx).
void IVP_Compact_Ledge_Solver::transform_pos_other_space(const IVP_U_Float_Point* Pos, const IVP_Cache_Ledge_Point* CacheIn,
                                                         const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutPos)
{
    const IVP_U_Matrix& M = CacheIn->cache_object->WorldTransform;

    const float Ax0 = (float)((double)(M.rows[0].k[0]) * Pos->X);
    const float Ay0 = (float)((double)(M.rows[1].k[0]) * Pos->X);
    const double Az0 = (double)(Pos->X) * M.rows[2].k[0];
    const float Ax1 = (float)((double)(M.rows[0].k[1]) * Pos->Y + Ax0);
    const float Ay1 = (float)((double)(M.rows[1].k[1]) * Pos->Y + Ay0);
    const double Az1 = Az0 + (double)(M.rows[2].k[1]) * Pos->Y;
    const float Wx = (float)(((double)(M.rows[0].k[2]) * Pos->Z + Ax1) + M.vv.k[0]);
    const float Wy = (float)(((double)(M.rows[1].k[2]) * Pos->Z + Ay1) + M.vv.k[1]);
    const double Wz = (Az1 + (double)(M.rows[2].k[2]) * Pos->Z) + M.vv.k[2];

    const IVP_U_Matrix& O = CacheOther->cache_object->WorldTransform;
    const float Dx = (float)((double)(Wx) - O.vv.k[0]);
    const float Dy = (float)((double)(Wy) - O.vv.k[1]);
    const double Dz = Wz - O.vv.k[2];

    OutPos->k[0] = (float)(((double)(O.rows[2].k[0]) * Dz + (double)(O.rows[1].k[0]) * Dy) +
                                      (double)(O.rows[0].k[0]) * Dx);
    OutPos->k[1] = (float)(((double)(O.rows[2].k[1]) * Dz + (double)(O.rows[1].k[1]) * Dy) +
                                      (double)(O.rows[0].k[1]) * Dx);
    OutPos->k[2] = (float)(((double)(O.rows[2].k[2]) * Dz + (double)(O.rows[1].k[2]) * Dy) +
                                      (double)(O.rows[0].k[2]) * Dx);
}

// FUN_0635b8d0 (.text:0635b8d0-0635b932), cdecl (edge, ledge, out): the three start points
// start(edge), start(prev edge) (DAT_063ee4f0), start(next edge) (DAT_063ee4e0) of the ledge;
// FUN_063480a0(out; p0, p_prev, p_next) then FUN_06348150(out).
void IVP_Compact_Ledge_Solver::calc_hesse_object(const IVP_Compact_Edge* Edge, const IVP_Compact_Ledge* Ledge,
                                                 IVP_U_Point* OutHesse)
{
    const IVP_U_Float_Point* const P0 = Ledge->GetPoint(Edge);
    const IVP_U_Float_Point* const PPrev = Ledge->GetPoint(Edge->GetPrevEdge());
    const IVP_U_Float_Point* const PNext = Ledge->GetPoint(Edge->GetNextEdge());
    OutHesse->set_plane_from_3_points(reinterpret_cast<const IVP_U_Point*>(P0), reinterpret_cast<const IVP_U_Point*>(PPrev),
                                      reinterpret_cast<const IVP_U_Point*>(PNext));
    OutHesse->normize_plane();
}

// FUN_0635b3b0 (.text:0635b3b0-0635b4c5), thiscall (K, L, cacheK, cacheL), RET 0x10.
//   :0635b3c0 +0x48 = K, +0x4c = L.
//   :0635b3c6-:0635b423 k = float(start(next K) - start(K)) from cacheK's points (y, z, x).
//   :0635b427 FUN_0635af70(&k, cacheK, cacheL, this + 0x28); :0635b436 +0x50 = cacheK, +0x54 = cacheL.
//   :0635b43c / :0635b458 FUN_0635ae60(K / next(K), cacheK, cacheL, this + 0x08 / + 0x18).
//   :0635b45d-:0635b4b4 +0x00 / +0x04 = cacheL's points of L / next(L); +0x38 = float(L1 - L0).
//   :0635b4b7 FUN_0628bed0(this + 0x58, this + 0x28, this + 0x38).
IVP_KK_Input::IVP_KK_Input(const IVP_Compact_Edge* KIn, const IVP_Compact_Edge* LIn, const IVP_Cache_Ledge_Point* CacheKIn,
                           const IVP_Cache_Ledge_Point* CacheLIn)
{
    K = KIn;
    L = LIn;

    const IVP_U_Float_Point* const PointsK = CacheKIn->compact_poly_points;
    const IVP_U_Float_Point* const K0 = PointsK + KIn->GetStartPointIndex();
    const IVP_U_Float_Point* const K1 = PointsK + KIn->GetNextEdge()->GetStartPointIndex();
    IVP_U_Point KVec;
    KVec.k[1] = (float)((double)(K1->Y) - K0->Y);
    KVec.k[2] = (float)((double)(K1->Z) - K0->Z);
    KVec.k[0] = (float)((double)(K1->X) - K0->X);
    IVP_Compact_Ledge_Solver::transform_vec_other_space(&KVec, CacheKIn, CacheLIn, &K_vec_Los);

    cache_K = CacheKIn;
    cache_L = CacheLIn;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(KIn, CacheKIn, CacheLIn, &K_Los[0]);
    IVP_Compact_Ledge_Solver::calc_pos_other_space(KIn->GetNextEdge(), CacheKIn, CacheLIn, &K_Los[1]);

    const IVP_U_Float_Point* const PointsL = CacheLIn->compact_poly_points;
    const IVP_U_Float_Point* const L0 = PointsL + L->GetStartPointIndex();
    const IVP_U_Float_Point* const L1 = PointsL + L->GetNextEdge()->GetStartPointIndex();
    L_Los[0] = L0;
    L_Los[1] = L1;
    L_vec_Los.k[1] = (float)((double)(L1->Y) - L0->Y);
    L_vec_Los.k[2] = (float)((double)(L1->Z) - L0->Z);
    L_vec_Los.k[0] = (float)((double)(L1->X) - L0->X);

    IVP_CalcCrossProduct(&cross_KL_Los, &K_vec_Los, &L_vec_Los);
}

// FUN_0635b4d0 (.text:0635b4d0-0635b566), thiscall, returns on the x87 stack.
//   :0635b4d0-:0635b4fa q = (cx*cx + cy*cy) + cz*cz of cross_KL_Los (stack); FCOM 1e-12f
//     (DAT_063a5494); TEST AH,0x41; JNZ -> q <= 1e-12 (or unordered) takes the fallback.
//   :0635b4fc-:0635b539 d = ((K0z*cz + K0y*cy) + K0x*cx) - ((L0z*cz + L0y*cy) + L0x*cx);
//     returns (d * d) / q.
//   :0635b53a-:0635b562 fallback: FUN_0635bd10(&K_Los[0], ledge of L's triangle, L).
double IVP_KK_Input::calc_quad_distance_edge_edge() const
{
    const double Cx = cross_KL_Los.k[0];
    const double Cy = cross_KL_Los.k[1];
    const double Cz = cross_KL_Los.k[2];
    const double QuadCross = (Cx * Cx + Cy * Cy) + Cz * Cz;
    if (QuadCross > (double)(1.0e-12f))
    {
        const double DistK = (K_Los[0].k[2] * Cz + K_Los[0].k[1] * Cy) + K_Los[0].k[0] * Cx;
        const double DistL = (L_Los[0]->Z * Cz + L_Los[0]->Y * Cy) + L_Los[0]->X * Cx;
        const double Dist = DistK - DistL;
        return (Dist * Dist) / QuadCross;
    }
    return IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(&K_Los[0], L->GetCompactLedge(), L);
}

// FUN_0635b570 (.text:0635b570-0635b8ca), cdecl (in, result), returns EAX.
//   :0635b57e-:0635b5ab q = (cx*cx + cy*cy) + cz*cz; FCOMP 1e-6f (DAT_063a5610); TEST AH,0x41;
//     JNZ -> q <= 1e-6 (or unordered) takes the sampled branch.
//   :0635b5b1-:0635b5c8 nK = K_vec_Los x c, nL = L_vec_Los x c (FUN_0628bed0).
//   :0635b5cd-:0635b63c a = nK.L0 (stack), b = float(nK.L1), c0 = float(nK.K0) (each dot is
//     (y*y' + z*z') + x*x'); s = float(a - b); checks_L[0] = float((a - c0) * s),
//     checks_L[1] = float((c0 - b) * s).
//   :0635b64e-:0635b6c4 d = nL.K0 (stack), e = float(nL.K1), f = float(nL.L0); s = float(d - e);
//     checks_K[0] = float((d - f) * s), checks_K[1] = float((f - e) * s). Returns 1.
//   :0635b6d3-:0635b7ed min = 1e20f; for the first 11 sample parameters t: p = K0 (1 - t) + K1 t
//     (FUN_06347e00 on K_Los); d = FUN_0635ba10(cacheL ledge, L, p); FCOM min; TEST AH,1 -> d < min
//     (or unordered): min = float(d), checks_K = {t, float(1 - t)}, checks_L = FUN_0635b150(
//     cacheL ledge, L, p).
//   :0635b7f3-:0635b819 FUN_0635b030 carries L0 and L1 into K's space.
//   :0635b827-:0635b8b8 for the first 9 parameters, the same with p on L (in K space) against
//     K in cacheK's ledge: checks_L = {t, float(1 - t)}, checks_K = s values. min is not reset.
//     Returns 0.
int32_t IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(const IVP_KK_Input* In, IVP_Unscaled_KK_Result* OutResult)
{
    const IVP_U_Point* const C = &In->cross_KL_Los;
    const double QuadCross = ((double)(C->k[0]) * C->k[0] + (double)(C->k[1]) * C->k[1]) +
                             (double)(C->k[2]) * C->k[2];
    if (QuadCross > (double)(1.0e-6f))
    {
        IVP_U_Point NormalK;
        IVP_CalcCrossProduct(&NormalK, &In->K_vec_Los, C);
        IVP_U_Point NormalL;
        IVP_CalcCrossProduct(&NormalL, &In->L_vec_Los, C);

        const IVP_U_Float_Point* const L0 = In->L_Los[0];
        const IVP_U_Float_Point* const L1 = In->L_Los[1];
        const IVP_U_Point* const K0 = &In->K_Los[0];
        const IVP_U_Point* const K1 = &In->K_Los[1];

        const double DistL0 = ((double)(NormalK.k[1]) * L0->Y + (double)(NormalK.k[2]) * L0->Z) +
                              (double)(NormalK.k[0]) * L0->X;
        const float DistL1 = (float)(
            ((double)(NormalK.k[1]) * L1->Y + (double)(NormalK.k[2]) * L1->Z) +
            (double)(NormalK.k[0]) * L1->X);
        const float DistK0 = (float)(
            ((double)(NormalK.k[1]) * K0->k[1] + (double)(NormalK.k[2]) * K0->k[2]) +
            (double)(NormalK.k[0]) * K0->k[0]);
        const float ScaleL = (float)(DistL0 - DistL1);
        OutResult->checks_L[0] = (float)((DistL0 - DistK0) * ScaleL);
        OutResult->checks_L[1] = (float)(((double)(DistK0) - DistL1) * ScaleL);

        const double DistK0OnL = ((double)(NormalL.k[1]) * K0->k[1] + (double)(NormalL.k[2]) * K0->k[2]) +
                                 (double)(NormalL.k[0]) * K0->k[0];
        const float DistK1OnL = (float)(
            ((double)(NormalL.k[1]) * K1->k[1] + (double)(NormalL.k[2]) * K1->k[2]) +
            (double)(NormalL.k[0]) * K1->k[0]);
        const float DistL0OnL = (float)(
            ((double)(NormalL.k[1]) * L0->Y + (double)(NormalL.k[2]) * L0->Z) +
            (double)(NormalL.k[0]) * L0->X);
        const float ScaleK = (float)(DistK0OnL - DistK1OnL);
        OutResult->checks_K[0] = (float)((DistK0OnL - DistL0OnL) * ScaleK);
        OutResult->checks_K[1] = (float)(((double)(DistL0OnL) - DistK1OnL) * ScaleK);
        return 1;
    }

    float MinQuadDist = 1.0e20f; // 0x60ad78ec
    const IVP_Compact_Ledge* const LedgeL = In->cache_L->compact_ledge;
    for (int32_t I = 0; I < 11; ++I)
    {
        const float T = GKKSampleParams[I];
        IVP_U_Point Pos;
        Pos.set_interpolate(&In->K_Los[0], &In->K_Los[1], T);
        const double QuadDist = quad_dist_edge_to_point_K_space(LedgeL, In->L, &Pos);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            OutResult->checks_K[0] = T;
            OutResult->checks_K[1] = (float)(1.0 - (double)(T));
            IVP_Unscaled_S_Result S;
            calc_unscaled_s_val_K_space(LedgeL, In->L, &Pos, &S);
            OutResult->checks_L[0] = S.Checks[0];
            OutResult->checks_L[1] = S.Checks[1];
        }
    }

    IVP_U_Point L0InK;
    transform_pos_other_space(In->L_Los[0], In->cache_L, In->cache_K, &L0InK);
    IVP_U_Point L1InK;
    transform_pos_other_space(In->L_Los[1], In->cache_L, In->cache_K, &L1InK);

    const IVP_Compact_Ledge* const LedgeK = In->cache_K->compact_ledge;
    for (int32_t I = 0; I < 9; ++I)
    {
        const float T = GKKSampleParams[I];
        IVP_U_Point Pos;
        Pos.set_interpolate(&L0InK, &L1InK, T);
        const double QuadDist = quad_dist_edge_to_point_K_space(LedgeK, In->K, &Pos);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            OutResult->checks_L[0] = T;
            OutResult->checks_L[1] = (float)(1.0 - (double)(T));
            IVP_Unscaled_S_Result S;
            calc_unscaled_s_val_K_space(LedgeK, In->K, &Pos, &S);
            OutResult->checks_K[0] = S.Checks[0];
            OutResult->checks_K[1] = S.Checks[1];
        }
    }
    return 0;
}

// FUN_0635bdd0 (.text:0635bdd0-0635bfef), cdecl (K, L, cacheK, cacheL), returns on the x87 stack.
//   :0635bdfe-:0635be0d IVP_KK_Input in(K, L, cacheK, cacheL); FUN_0635b570(&in, &r) (EAX unused).
//   :0635be12-:0635be23 no sign bit in (r.L0 | r.L1):
//     r.K0 not > 0 (TEST AH,0x41) -> FUN_0635bd10(&in.K_Los[0], cacheL ledge, L);
//     r.K1 not > 0 -> FUN_0635bd10(&in.K_Los[1], cacheL ledge, L); else FUN_0635b4d0(&in).
//   :0635be95-:0635bf2b else no sign bit in (r.K1 | r.K0):
//     r.L0 not > 0 -> FUN_0635ae60(L, cacheL, cacheK, &p), FUN_0635bd10(&p, cacheK ledge, K);
//     r.L1 not > 0 -> the same with next(L); else CORE (:0635bf2c) and fall through.
//   :0635bf36-:0635bfe1 min = 1e20f over i = 0, 1: p = FUN_0635ae60({L, next L}[i], cacheL, cacheK);
//     d = FUN_0635bd10(&p, cacheK ledge, K); then FUN_0635bd10(&in.K_Los[i], cacheL ledge, L);
//     each FCOM min; TEST AH,1 -> d < min (or unordered): min = float(d). Returns min.
double IVP_Compact_Ledge_Solver::calc_qlen_KK(const IVP_Compact_Edge* K, const IVP_Compact_Edge* L,
                                              const IVP_Cache_Ledge_Point* CacheK, const IVP_Cache_Ledge_Point* CacheL)
{
    const IVP_KK_Input In(K, L, CacheK, CacheL);
    IVP_Unscaled_KK_Result Result;
    calc_unscaled_KK_vals(&In, &Result);

    if (((IVP_SolverFloatBits(Result.checks_L[0]) | IVP_SolverFloatBits(Result.checks_L[1])) & 0x80000000u) == 0)
    {
        if (!(Result.checks_K[0] > 0.0f))
            return calc_qlen_PK_K_space(&In.K_Los[0], CacheL->compact_ledge, L);
        if (!(Result.checks_K[1] > 0.0f))
            return calc_qlen_PK_K_space(&In.K_Los[1], CacheL->compact_ledge, L);
        return In.calc_quad_distance_edge_edge();
    }

    if (((IVP_SolverFloatBits(Result.checks_K[1]) | IVP_SolverFloatBits(Result.checks_K[0])) & 0x80000000u) == 0)
    {
        if (!(Result.checks_L[0] > 0.0f))
        {
            IVP_U_Point Pos;
            calc_pos_other_space(L, CacheL, CacheK, &Pos);
            return calc_qlen_PK_K_space(&Pos, CacheK->compact_ledge, K);
        }
        if (!(Result.checks_L[1] > 0.0f))
        {
            IVP_U_Point Pos;
            calc_pos_other_space(L->GetNextEdge(), CacheL, CacheK, &Pos);
            return calc_qlen_PK_K_space(&Pos, CacheK->compact_ledge, K);
        }
        IVP_CoreTrap();
    }

    const IVP_Compact_Edge* const EdgesOfL[2] = { L, L->GetNextEdge() };
    float MinQuadDist = 1.0e20f; // 0x60ad78ec
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_U_Point Pos;
        calc_pos_other_space(EdgesOfL[I], CacheL, CacheK, &Pos);
        const double QuadDistL = calc_qlen_PK_K_space(&Pos, CacheK->compact_ledge, K);
        if (!(QuadDistL >= MinQuadDist))
            MinQuadDist = (float)(QuadDistL);
        const double QuadDistK = calc_qlen_PK_K_space(&In.K_Los[I], CacheL->compact_ledge, L);
        if (!(QuadDistK >= MinQuadDist))
            MinQuadDist = (float)(QuadDistK);
    }
    return MinQuadDist;
}
