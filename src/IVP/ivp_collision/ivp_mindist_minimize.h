// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP_Mindist_Minimize_Solver -- the closest-feature walk of an exact mindist
// (Ipion ivp_mindist_minimize). Retail builds one 0x81c-byte solver on the
// stack in IVP_Ledge_Mindist::DispatchNarrowPhaseSolve (06352cf0,
// P_Finish_Counter = 0x14) and IVP_Ledge_Mindist::recalc_mindist (06352b90,
// P_Finish_Counter = 0) and calls mms_function_table[status0][status1] with
// it, status0 / status1 being the sorted synapses' EIVP_Synapse_Type.
//
// Return codes (IVP_MRC_TYPE): the callers clear StateFlags 0x4000 / 0x8000 on
// OK, call proove_polypoly (06352b30) on BACKSIDE, and hit the library's CORE
// null store on anything other than OK / ENDLESS_LOOP / BACKSIDE.
//
// STATE (cmn-ivp2d, 2026-10-08): every member below has a body in
// ivp_mindist_minimize.cpp, ported from the listing. This header is in the
// build (ivp_compact_ledge_solver.cpp includes it for IVP_Cache_Ledge_Point);
// the .cpp joins Common.vcxproj together with the switch-over of 06352cf0 /
// 06352b90 (exclusive slot).

#include <cstdint>

#include "../ivp_utility/ivp_math.h"

class IVP_Ledge_Mindist;
class IVP_Real_Object;
class IVP_Compact_Edge;
class IVP_Compact_Ledge;
class IVP_Synapse_Real;
struct IVP_U_Float_Point;
struct IVP_NarrowPhaseCacheSlot; // the library's IVP_Cache_Object (0x70 bytes), ivp_real_object.h
class IVP_KK_Input;              // ivp_compact_ledge_solver.h
struct IVP_Unscaled_KK_Result;   // ivp_compact_ledge_solver.h

enum IVP_MRC_TYPE : int32_t
{
    IVP_MRC_UNINITIALIZED = 0,
    IVP_MRC_OK = 1,                 // 06352c49 / 06352d8a: flags &= 0xffff3fff, return 1
    IVP_MRC_ENDLESS_LOOP = 2,       // check_loop_hash hit (06363649 ...)
    IVP_MRC_BACKSIDE = 3,           // caller runs proove_polypoly (06352c9b / 06352db9)
    IVP_MRC_ALREADY_CALCULATED = 4, // 06352bc1 / 06352d21: same sim step
    IVP_MRC_ILLEGAL = 5             // minimize_illegal (06353930)
};

// The synapse status a converged backside walk leaves behind (06363904
// `MOV word [syn+0x1a],5`); proove_polypoly (06352b45) looks for it.
constexpr int16_t IVP_ST_BACKSIDE = 5;

// Per-side view of a polygon ledge, built on the handler's stack (06353370:
// side 0 at ESP+0xc, side 1 at ESP+0x20; 06353560: the polygon side at
// ESP+0xc). Layout from those stores.
struct IVP_Cache_Ledge_Point
{
    const IVP_U_Float_Point* compact_poly_points; // +0x00 ledge + ledge->CPointOffset
    const IVP_Compact_Ledge* compact_ledge;       // +0x04
    IVP_NarrowPhaseCacheSlot* cache_object;       // +0x08 object->ResolveNarrowPhaseCacheSlot() (06353510)
    IVP_Real_Object* object;                      // +0x0c synapse->l_obj
    IVP_Synapse_Real* synapse;                    // +0x10 the side's synapse in the mindist
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Cache_Ledge_Point) == 0x14, "IVP_Cache_Ledge_Point is 0x14 bytes in retail");

// The ball side of minimize_B_POLY (06353560: ESP+0, ESP+4, ESP+8).
struct IVP_Cache_Ball
{
    IVP_NarrowPhaseCacheSlot* cache_object; // +0x00
    IVP_Real_Object* object;                // +0x04 mindist->synapse[0].l_obj
    IVP_Synapse_Real* synapse;              // +0x08 &mindist->synapse[0]
};

class IVP_Mindist_Minimize_Solver
{
public:
    // One loop_hash record: the two keys check_loop_hash stores, larger first.
    struct FLoopHashEntry
    {
        int32_t Key0; // +0x00
        int32_t Key1; // +0x04
    };

    IVP_Ledge_Mindist* mindist;      // +0x000
    int32_t P_Finish_Counter;        // +0x004 decremented by each walk step; below zero the loop hash is consulted
    // +0x008: the polygon-space point of the point side, recorded by a walk
    // that converged on the back side of the triangle (06363620 :063638e4-
    // :063638f4); proove_polypoly (06352b30 :06352b6f) walks the pierce
    // chain toward it. Retail name not in the binary.
    IVP_U_Point BacksidePosition;
    FLoopHashEntry loop_hash[0x100]; // +0x018
    int32_t loop_hash_size;          // +0x818

    // DAT_0658ab4c, 4 x 4 cdecl handlers indexed [status0 * 4 + status1],
    // filled by init_mms_function_table (06353940, called from the
    // environment constructor at 0634c5c0).
    static IVP_MRC_TYPE (*mms_function_table[4][4])(IVP_Mindist_Minimize_Solver* Solver);
    static void init_mms_function_table(); // FUN_06353940

    // ---- table handlers (cdecl, one argument) ----
    static IVP_MRC_TYPE minimize_default_poly_poly(IVP_Mindist_Minimize_Solver* Solver); // FUN_06353370
    static IVP_MRC_TYPE minimize_B_POLY(IVP_Mindist_Minimize_Solver* Solver);            // FUN_06353560
    static IVP_MRC_TYPE minimize_swapped_B_POLY(IVP_Mindist_Minimize_Solver* Solver);    // LAB_06353680
    static IVP_MRC_TYPE minimize_swapped_poly_poly(IVP_Mindist_Minimize_Solver* Solver); // LAB_063536b0
    static IVP_MRC_TYPE minimize_BB(IVP_Mindist_Minimize_Solver* Solver);                // FUN_063536d0
    static IVP_MRC_TYPE minimize_illegal(IVP_Mindist_Minimize_Solver* Solver);           // LAB_06353930

    // FUN_06352a90 (thiscall, RET 0x10).
    int32_t check_loop_hash(int32_t Status0, const IVP_Compact_Edge* Edge0, int32_t Status1, const IVP_Compact_Edge* Edge1);

    // FUN_06352b30 (thiscall, no arguments): after a BACKSIDE result, move the
    // backside synapse to the triangle its pierce chain reaches.
    void proove_polypoly();

    // Edges are passed non-const: the walks store them into the synapses
    // (IVP_Synapse_Real::edge).

    // ---- point / edge / triangle (ported) ----
    // FUN_06363350 (RET 0x10).
    IVP_MRC_TYPE p_minimize_PF(IVP_Compact_Edge* P, IVP_Compact_Edge* F,
                               IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheF);
    // FUN_06363620 (RET 0x14): P projects inside F; walk P's vertex fan.
    IVP_MRC_TYPE p_minimize_Leave_PF(IVP_Compact_Edge* P, const IVP_U_Point* PosPInFSpace, IVP_Compact_Edge* F,
                                     IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheF);
    // FUN_063651c0 (RET 0x10).
    IVP_MRC_TYPE p_minimize_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                               IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK);

    // FUN_06364890 (RET 0x10).
    IVP_MRC_TYPE p_minimize_PP(IVP_Compact_Edge* P0, IVP_Compact_Edge* P1,
                               IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1);
    // FUN_06365280 (RET 0x10): P projects onto the inside of edge K.
    IVP_MRC_TYPE p_minimize_Leave_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                                     IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK);

    // FUN_06363ab0 (RET 0x10).
    IVP_MRC_TYPE p_minimize_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L,
                               IVP_Cache_Ledge_Point* CacheK, IVP_Cache_Ledge_Point* CacheL);
    // FUN_06363e00 (RET 0x18): the closest points lie inside both edges.
    IVP_MRC_TYPE p_minimize_Leave_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L, const IVP_KK_Input* KKInput,
                                     const IVP_Unscaled_KK_Result* Result, IVP_Cache_Ledge_Point* CacheK,
                                     IVP_Cache_Ledge_Point* CacheL);

    // FUN_06352e40 (RET 0x10): exhaustive feature search between two triangles.
    IVP_MRC_TYPE p_minimize_FF(IVP_Compact_Edge* F0, IVP_Compact_Edge* F1,
                               IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1);

    // ---- ball against point / edge / triangle ----
    // FUN_063645d0 (RET 0xc).
    IVP_MRC_TYPE p_minimize_BP(IVP_Cache_Ball* Ball, IVP_Compact_Edge* P, IVP_Cache_Ledge_Point* CacheP);
    // FUN_06364ce0 (RET 0xc).
    IVP_MRC_TYPE p_minimize_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K, IVP_Cache_Ledge_Point* CacheK);
    // FUN_06364d90 (RET 0xc): the ball centre projects onto the inside of edge K.
    IVP_MRC_TYPE p_minimize_Leave_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K, IVP_Cache_Ledge_Point* CacheK);
    // FUN_06363430 (RET 0xc).
    IVP_MRC_TYPE p_minimize_BF(IVP_Cache_Ball* Ball, IVP_Compact_Edge* F, IVP_Cache_Ledge_Point* CacheF);
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Mindist_Minimize_Solver) == 0x81c, "IVP_Mindist_Minimize_Solver is 0x81c bytes in retail");
