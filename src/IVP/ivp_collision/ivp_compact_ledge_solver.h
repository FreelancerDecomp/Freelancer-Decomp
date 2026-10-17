// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP_Compact_Ledge_Solver -- the stateless per-triangle / per-tree helpers of the compact
// ledge geometry code (Ipion IVP_CLS). Every function takes the RAW IVP_Compact_Ledge*
// (retail reads the point array as `ledge + *(int*)ledge`); no {ledge, matrix} wrapper.
// calc_hesse_vec_object_not_normized / calc_unscaled_qr_vals_F_space live in
// ivp_mindist_solve.cpp, the rest in ivp_compact_ledge_solver.cpp.

#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_utility/ivp_math.h"
#include "../ivp_physics/ivp_surface_manager.h" // IVP_SurfaceManager_OutList (IVP_U_BigVector)

class IVP_Compact_Surface;
class IVP_Compact_Ledgetree_Node;
struct IVP_Cache_Ledge_Point; // ivp_mindist_minimize.h
class IVP_KK_Input;           // below
struct IVP_Unscaled_KK_Result; // below

// Output of calc_unscaled_qr_vals_F_space (FUN_0635b210): four floats.
// Checks[i] is the unscaled barycentric weight of the vertex OPPOSITE the i-th edge counted
// from the edge passed in (i = 0: that edge, 1: its next, 2: next of next); a point lies
// over the triangle when all three are > 0. Det = |a|^2 |b|^2 - (a.b)^2, the common scale.
struct IVP_Unscaled_QR_Result
{
    float Checks[3]; // +0x00
    float Det;       // +0x0c
};

// Output of calc_unscaled_s_val_K_space (FUN_0635b150): the projection of a point onto an
// edge, unscaled. Checks[0] = (P1-P0).(p-P0), Checks[1] = (P1-P0).(P1-p); the projection
// lies on the edge when both are >= 0.
struct IVP_Unscaled_S_Result
{
    float Checks[2]; // +0x00
};

class IVP_Compact_Ledge_Solver
{
public:
    // FUN_0635b940 (cdecl): unnormalised triangle normal
    // (P(next) - P(edge)) x (P(prev) - P(edge)) in the ledge's object space; writes x, y, z.
    static void calc_hesse_vec_object_not_normized(const IVP_Compact_Edge* Edge, const IVP_Compact_Ledge* Ledge,
                                                   IVP_U_Point* OutNormal);

    // FUN_0635b210 (cdecl): unscaled barycentric weights of a point (object space of Ledge)
    // against the triangle that owns Edge.
    static void calc_unscaled_qr_vals_F_space(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Edge* Edge,
                                              const IVP_U_Point* Point, IVP_Unscaled_QR_Result* OutResult);

    // FUN_0635b150 (cdecl).
    static void calc_unscaled_s_val_K_space(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Edge* Edge,
                                            const IVP_U_Point* Point, IVP_Unscaled_S_Result* OutResult);

    // FUN_0635ba10 (cdecl): squared distance of Point from the edge's line; returned on the
    // x87 stack (double here).
    static double quad_dist_edge_to_point_K_space(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Edge* Edge,
                                                  const IVP_U_Point* Point);

    // FUN_0635bb40 (cdecl): squared distance of Point from the triangle (face, edges or
    // vertices); returned on the x87 stack (double here).
    static double calc_qlen_PF_F_space(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Triangle* Triangle,
                                       const IVP_U_Point* Point);

    // FUN_0635aba0 (cdecl): appends every terminal ledge below Node, left subtree first.
    static void get_all_ledges(const IVP_Compact_Ledgetree_Node* Node, IVP_SurfaceManager_OutList* OutLedges);

    // FUN_0635abf0 (cdecl): get_all_ledges from the surface's ledge-tree root.
    static void get_all_ledges(const IVP_Compact_Surface* Surface, IVP_SurfaceManager_OutList* OutLedges);

    // ---- cmn-ivp2c block (2026-10-08): helpers of IVP_Mindist_Minimize_Solver ----
    // FUN_0635ae60 (cdecl): the start point of P (in CacheP's ledge) carried to world space
    // by CacheP's cache object matrix and back into CacheOther's object space.
    static void calc_pos_other_space(const IVP_Compact_Edge* P, const IVP_Cache_Ledge_Point* CacheP,
                                     const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutPos);

    // FUN_0635bcc0 (cdecl): squared distance of Point from the start vertex of Edge;
    // returned on the x87 stack (double here).
    static double quad_dist_edge_start_to_point(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Edge* Edge,
                                                const IVP_U_Point* Point);

    // FUN_0635bd10 (cdecl): squared distance of Point from the edge SEGMENT (line inside
    // the s-range, else the nearer end vertex); double.
    static double calc_qlen_PK_K_space(const IVP_U_Point* Point, const IVP_Compact_Ledge* Ledge,
                                       const IVP_Compact_Edge* Edge);

    // FUN_0635bdd0 (cdecl, 4 args, returns on the x87 stack): squared distance between the
    // edge SEGMENTS K (CacheK's ledge) and L (CacheL's ledge), routed by the KK values.
    static double calc_qlen_KK(const IVP_Compact_Edge* K, const IVP_Compact_Edge* L,
                               const IVP_Cache_Ledge_Point* CacheK, const IVP_Cache_Ledge_Point* CacheL);

    // ---- cmn-ivp2d block (2026-10-08) ----
    // FUN_0635bda0 (cdecl): the start point of Edge (Cache's ledge) in world space, through
    // the cache object's matrix (FUN_0634d8d0).
    static void give_world_coords_AT(const IVP_Compact_Edge* Edge, const IVP_Cache_Ledge_Point* Cache,
                                     IVP_U_Point* OutWorld);

    // FUN_0635af70 (cdecl): a direction of CacheIn's object space rotated into CacheOther's
    // object space (world rotation of CacheIn, then the transposed rotation of CacheOther).
    static void transform_vec_other_space(const IVP_U_Point* Vec, const IVP_Cache_Ledge_Point* CacheIn,
                                          const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutVec);

    // FUN_0635b030 (cdecl): a point of CacheIn's object space carried into CacheOther's object
    // space; the same arithmetic as calc_pos_other_space, but takes the point itself.
    static void transform_pos_other_space(const IVP_U_Float_Point* Pos, const IVP_Cache_Ledge_Point* CacheIn,
                                          const IVP_Cache_Ledge_Point* CacheOther, IVP_U_Point* OutPos);

    // FUN_0635b8d0 (cdecl): the normalised Hesse plane (x, y, z, hesse) of Edge's triangle in
    // Ledge's object space, through start(Edge), start(prev Edge), start(next Edge).
    static void calc_hesse_object(const IVP_Compact_Edge* Edge, const IVP_Compact_Ledge* Ledge, IVP_U_Point* OutHesse);

    // FUN_0635b570 (cdecl, returns EAX): the unscaled edge / edge parameters. Returns 1 from
    // the analytic branch, 0 from the sampled one (near-parallel edges).
    static int32_t calc_unscaled_KK_vals(const IVP_KK_Input* In, IVP_Unscaled_KK_Result* OutResult);
};

// Output of calc_unscaled_KK_vals (FUN_0635b570), 4 floats. checks_K: where the closest point
// lies on K (both > 0: inside the segment), checks_L likewise on L. The sampled branch
// stores (t, 1 - t) for the sampled side and calc_unscaled_s_val_K_space for the other.
struct IVP_Unscaled_KK_Result
{
    float checks_K[2]; // +0x00
    float checks_L[2]; // +0x08
};

// IVP_KK_Input, 0x68 bytes, built on the caller's stack by FUN_0635b3b0 (thiscall, RET 0x10,
// returns this). Everything is expressed in L's object space (CacheL). Library (Ipion) names;
// none are in the binary.
class IVP_KK_Input
{
public:
    const IVP_U_Float_Point* L_Los[2];     // +0x00 / +0x04: start points of L and next(L)
    IVP_U_Point K_Los[2];                  // +0x08 / +0x18: start points of K and next(K), in L space
    IVP_U_Point K_vec_Los;                 // +0x28: float(K1 - K0) rotated into L space (0635af70)
    IVP_U_Point L_vec_Los;                 // +0x38: float(L1 - L0)
    const IVP_Compact_Edge* K;             // +0x48
    const IVP_Compact_Edge* L;             // +0x4c
    const IVP_Cache_Ledge_Point* cache_K;  // +0x50
    const IVP_Cache_Ledge_Point* cache_L;  // +0x54
    IVP_U_Point cross_KL_Los;              // +0x58: K_vec_Los x L_vec_Los (FUN_0628bed0)

    // FUN_0635b3b0.
    IVP_KK_Input(const IVP_Compact_Edge* KIn, const IVP_Compact_Edge* LIn, const IVP_Cache_Ledge_Point* CacheKIn,
                 const IVP_Cache_Ledge_Point* CacheLIn);

    // FUN_0635b4d0 (thiscall, returns on the x87 stack): squared distance between the two
    // edge LINES; near-parallel lines fall back to K's start point against the segment L.
    double calc_quad_distance_edge_edge() const;
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_KK_Input) == 0x68, "IVP_KK_Input is 0x68 bytes in retail");

// FUN_0628bed0 (IVP_CalcCrossProduct) lives in ../ivp_utility/ivp_math.h.
