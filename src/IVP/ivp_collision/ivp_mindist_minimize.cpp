// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Mindist_Minimize_Solver -- see ivp_mindist_minimize.h. Traced against
// EXE\Disasm\Common.dll.txt by cmn-ivp2c (2026-10-08) with retail's x87
// rounding points: values kept on the FPU stack are double, every
// `FSTP float ptr` is a float.
//
// All leaves are ported (cmn-ivp2c / cmn-ivp2d). This file joins
// Common.vcxproj together with the switch-over of 06352cf0 / 06352b90.

#include "ivp_mindist_minimize.h"

#include <cmath>
#include <cstring>

#include "ivp_compact_ledge_solver.h"
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../../include/common_ini.h" // Vector (IVP_Ledge_Mindist::SeparatingNormal)
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap: retail's `MOV dword ptr [0],0`

IVP_MRC_TYPE (*IVP_Mindist_Minimize_Solver::mms_function_table[4][4])(IVP_Mindist_Minimize_Solver* Solver) = {};

namespace
{
    uint32_t IVP_MinimizeFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // 0x60ad78ec: the "no candidate yet" start of the minimum searches.
    constexpr float MinimizeSearchStart = 1.0e20f;
}

// ---------------------------------------------------------------------------
// FUN_06353940 (.text:06353940-063539cd): REP STOSD fills all 16 slots of
// DAT_0658ab4c with LAB_06353930, then [status0][status1]:
//   row 0 (point):    default_poly_poly x3, swapped_B_POLY
//   row 1 (edge):     swapped_poly_poly, default_poly_poly, illegal, swapped_B_POLY
//   row 2 (triangle): swapped_poly_poly, illegal, default_poly_poly, swapped_B_POLY
//   row 3 (ball):     B_POLY x3, BB
// ---------------------------------------------------------------------------
void IVP_Mindist_Minimize_Solver::init_mms_function_table()
{
    for (int32_t I = 0; I < 4; ++I)
    {
        for (int32_t J = 0; J < 4; ++J)
            mms_function_table[I][J] = &minimize_illegal;
    }

    mms_function_table[0][0] = &minimize_default_poly_poly; // DAT_0658ab4c
    mms_function_table[0][1] = &minimize_default_poly_poly; // ab50
    mms_function_table[0][2] = &minimize_default_poly_poly; // ab54
    mms_function_table[0][3] = &minimize_swapped_B_POLY;    // ab58
    mms_function_table[1][0] = &minimize_swapped_poly_poly; // ab5c
    mms_function_table[1][1] = &minimize_default_poly_poly; // ab60
    mms_function_table[1][2] = &minimize_illegal;           // ab64
    mms_function_table[1][3] = &minimize_swapped_B_POLY;    // ab68
    mms_function_table[2][0] = &minimize_swapped_poly_poly; // ab6c
    mms_function_table[2][1] = &minimize_illegal;           // ab70
    mms_function_table[2][2] = &minimize_default_poly_poly; // ab74
    mms_function_table[2][3] = &minimize_swapped_B_POLY;    // ab78
    mms_function_table[3][0] = &minimize_B_POLY;            // ab7c
    mms_function_table[3][1] = &minimize_B_POLY;            // ab80
    mms_function_table[3][2] = &minimize_B_POLY;            // ab84
    mms_function_table[3][3] = &minimize_BB;                // ab88
}

// ---------------------------------------------------------------------------
// FUN_06352a90 (.text:06352a90-06352b04, thiscall, RET 0x10).
//   Key0 = a | b, Key1 = c | d; Key0 < Key1 (signed, JGE skips) -> swap.
//   Scan entries n-1 .. 0 (this + 0x18 + 8i): equal pair -> return 1.
//   n >= 0x100 -> return 1. Else store at entry n, ++n, return 0.
// ---------------------------------------------------------------------------
int32_t IVP_Mindist_Minimize_Solver::check_loop_hash(int32_t Status0, const IVP_Compact_Edge* Edge0, int32_t Status1,
                                                     const IVP_Compact_Edge* Edge1)
{
    int32_t Key0 = Status0 | static_cast<int32_t>(reinterpret_cast<intptr_t>(Edge0));
    int32_t Key1 = Status1 | static_cast<int32_t>(reinterpret_cast<intptr_t>(Edge1));
    if (Key0 < Key1)
    {
        const int32_t Swap = Key0;
        Key0 = Key1;
        Key1 = Swap;
    }

    for (int32_t I = loop_hash_size - 1; I >= 0; --I)
    {
        if (loop_hash[I].Key0 == Key0 && loop_hash[I].Key1 == Key1)
            return 1;
    }
    if (loop_hash_size >= 0x100)
        return 1;

    loop_hash[loop_hash_size].Key0 = Key0;
    loop_hash[loop_hash_size].Key1 = Key1;
    ++loop_hash_size;
    return 0;
}

// ---------------------------------------------------------------------------
// FUN_06352b30 (.text:06352b30-06352b86, thiscall). The sorted synapse 0
// ((flags >> 8) & 3) if its status is 5 (backside), else the other one
// ((flags ^ 0x100) >> 8 & 3); edge = FUN_06353840(edge, &BacksidePosition)
// (cdecl), status = 2.
// ---------------------------------------------------------------------------
void IVP_Mindist_Minimize_Solver::proove_polypoly()
{
    IVP_Ledge_Mindist* const Mindist = mindist;
    IVP_Synapse_Real* Synapse = Mindist->get_sorted_synapse(0);
    if (Synapse->status != IVP_ST_BACKSIDE)
        Synapse = Mindist->get_sorted_synapse(1);
    Synapse->edge = IVP_WalkPiercingTriangleChain(Synapse->edge, &BacksidePosition);
    Synapse->status = IVP_SYNAPSE_POLY;
}

// ---------------------------------------------------------------------------
// FUN_06353370 (.text:06353370-063534ef, cdecl, one argument).
//   Side 0 = sorted synapse 0 (ESI), side 1 = sorted synapse 1 (EDI).
//   Cache0 at ESP+0xc: points, ledge(edge0), FUN_06353510(obj0), obj0, &syn0;
//   Cache1 at ESP+0x20 likewise; the caches are resolved 0 then 1.
//   index = status1 + status0 * 4 (MOVSX); JA above 5 -> FUN_06352e40:
//     0 PP 06364890, 1 PK 063651c0, 2 PF 06363350, 5 KK 06363ab0,
//     3 / 4 / everything else FF 06352e40; all thiscall (edge0, edge1, &cache0, &cache1).
//   Then --cache0->refcount, --cache1->refcount; the leaf's EAX is returned.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_default_poly_poly(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;
    IVP_Synapse_Real* const Synapse0 = Mindist->get_sorted_synapse(0);
    IVP_Synapse_Real* const Synapse1 = Mindist->get_sorted_synapse(1);
    IVP_Compact_Edge* const Edge0 = Synapse0->edge;
    IVP_Compact_Edge* const Edge1 = Synapse1->edge;

    IVP_Cache_Ledge_Point Cache0;
    Cache0.compact_ledge = Edge0->GetCompactLedge();
    Cache0.cache_object = Synapse0->l_obj->ResolveNarrowPhaseCacheSlot(); // :063533e1
    Cache0.object = Synapse0->l_obj;
    Cache0.compact_poly_points = Cache0.compact_ledge->GetPointArray();
    Cache0.synapse = Synapse0;

    IVP_Cache_Ledge_Point Cache1;
    Cache1.compact_ledge = Edge1->GetCompactLedge();
    Cache1.cache_object = Synapse1->l_obj->ResolveNarrowPhaseCacheSlot(); // :06353419
    Cache1.object = Synapse1->l_obj;
    Cache1.compact_poly_points = Cache1.compact_ledge->GetPointArray();
    Cache1.synapse = Synapse1;

    IVP_MRC_TYPE Result;
    switch (static_cast<uint32_t>(Synapse1->status + Synapse0->status * 4))
    {
    case 0:
        Result = Solver->p_minimize_PP(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 1:
        Result = Solver->p_minimize_PK(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 2:
        Result = Solver->p_minimize_PF(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 5:
        Result = Solver->p_minimize_KK(Edge0, Edge1, &Cache0, &Cache1);
        break;
    default: // 3, 4 and above 5 (switchdataD_063534f0 / JA caseD_3)
        Result = Solver->p_minimize_FF(Edge0, Edge1, &Cache0, &Cache1);
        break;
    }

    --Cache0.cache_object->RefCount; // :063534da
    --Cache1.cache_object->RefCount; // :063534e1-:063534e8
    return Result;
}

// ---------------------------------------------------------------------------
// FUN_06353560 (.text:06353560-06353674, cdecl). The ball is always physical
// synapse 0 (+0x18, object +0x28), the polygon physical synapse 1 (+0x34).
//   :06353577-:063535b9 FUN_06353510 inlined for the ball object; ball cache
//     at ESP+0: {cache, obj0, &synapse[0]}.
//   :063535bc-:063535ff polygon cache at ESP+0xc: points, ledge(edge1), cache
//     (FUN_06353510), obj1, &synapse[1].
//   switch synapse[1].status: 0 -> 063645d0, 1 -> 06364ce0, 2 -> 06363430, all
//   thiscall (&ball, edge1, &cache); else XOR EAX,EAX + CORE (:06353612).
//   Then --polygon cache refcount, --ball cache refcount.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_B_POLY(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;

    IVP_Cache_Ball Ball;
    Ball.object = Mindist->synapse[0].l_obj;
    Ball.cache_object = Ball.object->ResolveNarrowPhaseCacheSlot();
    Ball.synapse = &Mindist->synapse[0];

    IVP_Synapse_Real* const SynapseP = &Mindist->synapse[1];
    IVP_Compact_Edge* const Edge = SynapseP->edge;
    IVP_Cache_Ledge_Point CacheP;
    CacheP.compact_ledge = Edge->GetCompactLedge();
    CacheP.cache_object = SynapseP->l_obj->ResolveNarrowPhaseCacheSlot(); // :063535ea
    CacheP.object = SynapseP->l_obj;
    CacheP.compact_poly_points = CacheP.compact_ledge->GetPointArray();
    CacheP.synapse = SynapseP;

    IVP_MRC_TYPE Result;
    switch (SynapseP->status)
    {
    case 0:
        Result = Solver->p_minimize_BP(&Ball, Edge, &CacheP);
        break;
    case 1:
        Result = Solver->p_minimize_BK(&Ball, Edge, &CacheP);
        break;
    case 2:
        Result = Solver->p_minimize_BF(&Ball, Edge, &CacheP);
        break;
    default:
        Result = IVP_MRC_UNINITIALIZED;
        IVP_CoreTrap();
        break;
    }

    --CacheP.cache_object->RefCount; // :0635365f
    --Ball.cache_object->RefCount;   // :06353662-:0635366d
    return Result;
}

// LAB_06353680: CORE first, then flags ^= 0x100 and minimize_B_POLY.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_swapped_B_POLY(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_CoreTrap();
    Solver->mindist->StateFlags ^= 0x100u;
    return minimize_B_POLY(Solver);
}

// LAB_063536b0: flags ^= 0x100, then minimize_default_poly_poly.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_swapped_poly_poly(IVP_Mindist_Minimize_Solver* Solver)
{
    Solver->mindist->StateFlags ^= 0x100u;
    return minimize_default_poly_poly(Solver);
}

// LAB_06353930: CORE, return 5.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_illegal(IVP_Mindist_Minimize_Solver* /*Solver*/)
{
    IVP_CoreTrap();
    return IVP_MRC_ILLEGAL;
}

// ---------------------------------------------------------------------------
// FUN_063536d0 (.text:063536d0-06353837, cdecl). FUN_06353510 inlined twice
// (object 0 then object 1, physical order).
//   :0635376b-:06353790 n = float(c0.vv - c1.vv) per component -> mindist+0x60.
//   :06353793-:063537b2 q = (x*x + y*y) + z*z from the stored floats.
//   :063537b4-:063537d7 FABS; FCOMP 1e-10f; TEST AH,0x41; JNZ -> |q| <= eps or
//     unordered: k = 1.0f; else k = 1.0f / sqrt(q).
//   :063537d7-:063537de +0x54 = float(k*q - (+0x50)).
//   :063537e1-:063537f7 n.i = float(k * n.i).
//   :063537fe-:06353823 +0x58 = float((dy*ny + dz*nz) + dx*nx), d = c0.+0x5c - c1.+0x5c
//     kept on the stack.
//   Releases both caches, returns 1.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_BB(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;
    IVP_NarrowPhaseCacheSlot* const Cache0 = Mindist->synapse[0].l_obj->ResolveNarrowPhaseCacheSlot();
    IVP_NarrowPhaseCacheSlot* const Cache1 = Mindist->synapse[1].l_obj->ResolveNarrowPhaseCacheSlot();

    Vector& Normal = Mindist->SeparatingNormal;
    Normal.y = (float)((double)(Cache0->WorldTransform.vv.k[1]) - Cache1->WorldTransform.vv.k[1]);
    Normal.z = (float)((double)(Cache0->WorldTransform.vv.k[2]) - Cache1->WorldTransform.vv.k[2]);
    Normal.x = (float)((double)(Cache0->WorldTransform.vv.k[0]) - Cache1->WorldTransform.vv.k[0]);

    const double QuadLength = ((double)(Normal.x) * Normal.x + (double)(Normal.y) * Normal.y) +
                              (double)(Normal.z) * Normal.z;
    double Factor;
    if (std::fabs(QuadLength) > (double)(1.0e-10f)) // DAT_063a53d8
        Factor = 1.0 / std::sqrt(QuadLength);
    else
        Factor = 1.0; // DAT_063a53d0

    Mindist->SeparationGap = (float)(Factor * QuadLength - Mindist->CombinedRadius);
    Normal.x = (float)(Factor * Normal.x);
    Normal.y = (float)(Factor * Normal.y);
    Normal.z = (float)(Factor * Normal.z);

    const double Dx = (double)(Cache0->Secondary.x) - Cache1->Secondary.x;
    const double Dy = (double)(Cache0->Secondary.y) - Cache1->Secondary.y;
    const double Dz = (double)(Cache0->Secondary.z) - Cache1->Secondary.z;
    Mindist->SecondaryNormalDot = (float)((Dy * Normal.y + Dz * Normal.z) + Dx * Normal.x);

    --Cache0->RefCount; // :06353826
    --Cache1->RefCount; // :06353829-:0635382e
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363350 (.text:06363350-06363427, thiscall, RET 0x10) (P, F, &cacheP, &cacheF).
//   :06363371 FUN_0635ae60(P, cacheP, cacheF, &pos) -- P in F's object space.
//   :06363389 FUN_0635b210(cacheF->ledge, F, &pos, &qr).
//   :0636338e-:063633a7 no sign bit in (qr1 | qr2 | qr0) -> 06363620(P, &pos, F, cacheP, cacheF).
//   :063633c7-:06363409 else min = 1e20f, best = 0; three edges of F (next via
//     DAT_063ee4e0): d = FUN_0635bd10(&pos, cacheF->ledge, e); FCOM min; TEST AH,1;
//     C0 (less or unordered) -> min = float(d), best = e.
//   :0636341b 063651c0(P, best, cacheP, cacheF).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PF(IVP_Compact_Edge* P, IVP_Compact_Edge* F,
                                                        IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheF)
{
    IVP_U_Point PosPInFSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheF, &PosPInFSpace);
    IVP_Unscaled_QR_Result QR;
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &PosPInFSpace, &QR);

    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
        return p_minimize_Leave_PF(P, &PosPInFSpace, F, CacheP, CacheF);

    float MinQuadDist = MinimizeSearchStart;
    IVP_Compact_Edge* BestEdge = nullptr;
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 3; I != 0; --I)
    {
        const double QuadDist = IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(&PosPInFSpace, CacheF->compact_ledge, Edge);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            BestEdge = Edge;
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_PK(P, BestEdge, CacheP, CacheF);
}

// ---------------------------------------------------------------------------
// FUN_06363620 (.text:06363620-06363aa4, thiscall, RET 0x14)
// (P, &pos of P in F space, F, &cacheP, &cacheF).
//   :06363627-:06363655 if (--P_Finish_Counter < 0) and check_loop_hash(0, P, 2, F)
//     -> return 2.
//   :06363671-:063636a5 h = FUN_0635b940(F, cacheF->ledge); normize (06347ef0);
//     hw = cacheF->cache rotate (0634d9d0); hp = cacheP->cache inverse rotate (0634d970).
//   :063636aa-:063636f5 +0x54 = float(((hz*pz + hx*px) + hy*py) - ((hy*Fy + hz*Fz) + hx*Fx)),
//     F = start vertex of F in cacheF.
//   :063636f8-:0636370f contact plane (+0x60) = hw.
//   :06363716-:06363757 +0x54 < 0 (FCOMP 0, TEST AH,1; unordered too) -> hp = float(-1 * hp).
//   :0636375b-:06363771 +0x54 = float(+0x54 - (+0x50)).
//   :06363774-:063637ac +0x58 = float((dy*ny + dz*nz) + dx*nx), d = cacheP->cache.+0x5c -
//     cacheF->cache.+0x5c on the stack.
//   :063637af-:06363892 P's vertex fan: stop = prev(P); e = opposite(stop), then
//     Q = prev(e) each round, e = opposite(Q). v = start(Q) - start(P): x, z on the stack,
//     y spilled as float. dot = (hpz*vz + hpy*vy) + hpx*vx, FST float; dot < 0 (or
//     unordered): val = 06347df0(float((vx*vx + vy*vy) + vz*vz)) * float(dot); val < min
//     (or unordered) -> min = float(val), best = Q (min starts at 0.0f, best at 0).
//     Q == stop ends the walk.
//   :06363897-:06363922 no best: cacheP->synapse = {P, status 0}, cacheF->synapse =
//     {F, status 2}; (+0x54 + +0x50) < 0 (or unordered): BacksidePosition = pos (x, y, z),
//     cacheF->synapse = {F, status 5}, return 3; else return 1.
//   :06363925-:06363981 best: FUN_0635ae60(best, cacheP, cacheF, &q); FUN_0635b210(ledge of
//     F's own triangle, F, &q, &qr); no sign bit in (qr1 | qr2 | qr0) -> 06363620(best, &q,
//     F, cacheP, cacheF).
//   :063639a5-:063639f2 count qr[i] < 0 (TEST AH,1: or unordered); exactly one -> the
//     first such edge of F (next per step) -> 06363ab0(best, edge, cacheP, cacheF).
//   :063639f4-:06363a98 else min = 1e20f, k = 0; for the three edges of F whose qr[i] is
//     not > 0 (FCOMP 0; TEST AH,0x41; JZ skips greater): d = FUN_0635bdd0(best, edge,
//     cacheP, cacheF); d < min (or unordered) -> min = float(d), k = edge. Then
//     06363ab0(best, k, cacheP, cacheF).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_PF(IVP_Compact_Edge* P, const IVP_U_Point* PosPInFSpace,
                                                              IVP_Compact_Edge* F, IVP_Cache_Ledge_Point* CacheP,
                                                              IVP_Cache_Ledge_Point* CacheF)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P, IVP_SYNAPSE_POLY, F) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point HesseF;
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &HesseF);
    HesseF.normize();
    IVP_U_Point HesseWorld;
    IVP_RotateSlotToWorldSpace(CacheF->cache_object, &HesseF, &HesseWorld); // 0634d9d0
    IVP_U_Point HesseP;
    IVP_RotateWorldToSlotSpace(CacheP->cache_object, &HesseWorld, &HesseP); // 0634d970

    IVP_Ledge_Mindist* const Mindist = mindist;
    const IVP_U_Float_Point* const PointOfF = CacheF->compact_poly_points + F->GetStartPointIndex();
    const double DistP = ((double)(HesseF.k[2]) * PosPInFSpace->k[2] + (double)(HesseF.k[0]) * PosPInFSpace->k[0]) +
                         (double)(HesseF.k[1]) * PosPInFSpace->k[1];
    const double DistF = ((double)(HesseF.k[1]) * PointOfF->Y + (double)(HesseF.k[2]) * PointOfF->Z) +
                         (double)(HesseF.k[0]) * PointOfF->X;
    Mindist->SeparationGap = (float)(DistP - DistF);
    Mindist->SeparatingNormal.x = HesseWorld.k[0];
    Mindist->SeparatingNormal.y = HesseWorld.k[1];
    Mindist->SeparatingNormal.z = HesseWorld.k[2];

    if (!(Mindist->SeparationGap >= 0.0f)) // DAT_063a53d4
    {
        HesseP.k[0] = (float)((double)(HesseP.k[0]) * -1.0f); // DAT_063a5498
        HesseP.k[1] = (float)((double)(HesseP.k[1]) * -1.0f);
        HesseP.k[2] = (float)((double)(HesseP.k[2]) * -1.0f);
    }
    Mindist->SeparationGap = (float)((double)(Mindist->SeparationGap) - Mindist->CombinedRadius);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectF = CacheF->cache_object;
        const double Dx = (double)(CacheObjectP->Secondary.x) - CacheObjectF->Secondary.x;
        const double Dy = (double)(CacheObjectP->Secondary.y) - CacheObjectF->Secondary.y;
        const double Dz = (double)(CacheObjectP->Secondary.z) - CacheObjectF->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Dy * Mindist->SeparatingNormal.y + Dz * Mindist->SeparatingNormal.z) +
                                                         Dx * Mindist->SeparatingNormal.x);
    }

    // P's vertex fan.
    const IVP_U_Float_Point* const PointOfP = CacheP->compact_poly_points + P->GetStartPointIndex();
    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    float MinValue = 0.0f;
    IVP_Compact_Edge* BestEdge = nullptr;
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double Vx = (double)(Q->X) - PointOfP->X;
        const float Vy = (float)((double)(Q->Y) - PointOfP->Y);
        const double Vz = (double)(Q->Z) - PointOfP->Z;
        const double DotExtended = ((double)(HesseP.k[2]) * Vz + (double)(HesseP.k[1]) * Vy) +
                                   (double)(HesseP.k[0]) * Vx;
        const float Dot = (float)(DotExtended);
        if (!(DotExtended >= 0.0))
        {
            const float QuadLength = (float)((Vx * Vx + (double)(Vy) * Vy) + Vz * Vz);
            const double Value = IVP_InvSqrtFloat(QuadLength) * Dot;
            if (!(Value >= MinValue))
            {
                MinValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    if (BestEdge == nullptr)
    {
        CacheP->synapse->edge = P;
        CacheP->synapse->status = IVP_SYNAPSE_POINT;
        CacheF->synapse->edge = F;
        CacheF->synapse->status = IVP_SYNAPSE_POLY;
        if (!((double)(Mindist->SeparationGap) + Mindist->CombinedRadius >= 0.0))
        {
            BacksidePosition.k[0] = PosPInFSpace->k[0];
            BacksidePosition.k[1] = PosPInFSpace->k[1];
            BacksidePosition.k[2] = PosPInFSpace->k[2];
            CacheF->synapse->edge = F;
            CacheF->synapse->status = IVP_ST_BACKSIDE;
            return IVP_MRC_BACKSIDE;
        }
        return IVP_MRC_OK;
    }

    IVP_U_Point PosBestInFSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(BestEdge, CacheP, CacheF, &PosBestInFSpace);
    IVP_Unscaled_QR_Result QR;
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(F->GetCompactLedge(), F, &PosBestInFSpace, &QR);
    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
        return p_minimize_Leave_PF(BestEdge, &PosBestInFSpace, F, CacheP, CacheF);

    int32_t NegativeCount = 0;
    for (int32_t I = 0; I < 3; ++I)
    {
        if (!(QR.Checks[I] >= 0.0f))
            ++NegativeCount;
    }
    if (NegativeCount == 1)
    {
        IVP_Compact_Edge* Edge = F;
        for (int32_t I = 0; I < 3; ++I)
        {
            if (!(QR.Checks[I] >= 0.0f))
                return p_minimize_KK(BestEdge, Edge, CacheP, CacheF);
            Edge = Edge->GetNextEdge();
        }
    }

    IVP_Compact_Edge* BestOfF = nullptr;
    float MinDist = MinimizeSearchStart;
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 0; I < 3; ++I)
    {
        if (!(QR.Checks[I] > 0.0f))
        {
            const double Dist = IVP_Compact_Ledge_Solver::calc_qlen_KK(BestEdge, Edge, CacheP, CacheF);
            if (!(Dist >= MinDist))
            {
                MinDist = (float)(Dist);
                BestOfF = Edge;
            }
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_KK(BestEdge, BestOfF, CacheP, CacheF);
}

// ---------------------------------------------------------------------------
// FUN_063651c0 (.text:063651c0-06365272, thiscall, RET 0x10) (P, K, &cacheP, &cacheK).
//   :063651dd FUN_0635ae60(P, cacheP, cacheK, &pos).
//   :063651f5 FUN_0635b150(cacheK->ledge, K, &pos, &s).
//   :063651fa-:0636520c s0 < 0 (FCOMP 0, TEST AH,1; unordered too) -> 06364890(P, K, cacheP, cacheK).
//   :06365227-:06365238 s1 < 0 (likewise) -> 06364890(P, next(K), cacheP, cacheK).
//   :0636525e-:06365266 else 06365280(P, K, cacheP, cacheK).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                                                        IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK)
{
    IVP_U_Point PosPInKSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheK, &PosPInKSpace);
    IVP_Unscaled_S_Result S;
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &PosPInKSpace, &S);
    if (!(S.Checks[0] >= 0.0f))
        return p_minimize_PP(P, K, CacheP, CacheK);
    if (!(S.Checks[1] >= 0.0f))
        return p_minimize_PP(P, K->GetNextEdge(), CacheP, CacheK);
    return p_minimize_Leave_PK(P, K, CacheP, CacheK);
}

// ---------------------------------------------------------------------------
// FUN_06364890 (.text:06364890-06364cd3, thiscall, RET 0x10) (P0, P1, &cache0, &cache1).
//   :0636489b-:063648d5 if (--P_Finish_Counter < 0) and check_loop_hash(0, P0, 0, P1) -> return 2.
//   :063648e6-:0636493c w0 / w1 = FUN_0635bda0(P0 / P1) (world); local_20 = w0 in cache1's
//     space, local_10 = w1 in cache0's space (FUN_0634d860, ECX = the cache object).
//   :06364941-:06364982 d = w0 - w1 (stack); q = float((dz*dz + dy*dy) + dx*dx); FCOMP 1e-6f
//     (DAT_063a5610); TEST AH,0x41; JNZ -> q <= 1e-6 (or unordered): return 2.
//   :06364988-:063649e9 inv = 1 / sqrt(q) (stack); +0x54 = float(inv*q - (+0x50));
//     +0x60 = float(d * inv) per component.
//   :063649ec-:06364a1f +0x58 = float((dy*ny + dz*nz) + dx*nx), d = cache0.+0x5c - cache1.+0x5c.
//   :06364a22-:06364ba7 for side i = 0, 1 (the other point in side i's space, side i's
//     vertex V): v = float(pos - V); vV = float((vx*Vx + vy*Vy) + vz*Vz); walk V's fan as in
//     06363620 (Q = prev(opposite(...)), ending on prev(P)): p = ((vx*Qx + vy*Qy) + vz*Qz) - vV,
//     FST float; p > 0 (TEST AH,0x41 on the extended value): val = 06347df0(float((wz*wz +
//     wy*wy) + wx*wx), w = Q - V) * float(p); val > max (TEST AH,0x41; max starts at 0.0f):
//     max = float(val), best = {Q, side i's cache, the other side's edge and cache, pos}.
//   :06364bad-:06364be7 best: FUN_0635b150(best cache ledge, Q, pos, &s); s1 > 0 (TEST AH,0x41)
//     continues, else (and with no best) :06364be9.
//   :06364be9-:06364c28 cache0 synapse = {P0, 0}, cache1 synapse = {P1, 0}; return 1.
//   :06364c2b-:06364c8c s0 < 0 (TEST AH,1; or unordered): sorted synapse 0 made best cache's
//     (flags ^= 0x100 when it is not); 06364890(Q, other edge, best cache, other cache).
//   :06364c8f-:06364cd3 else sorted synapse 0 made the other side's; 06365280(other edge, Q,
//     other cache, best cache).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PP(IVP_Compact_Edge* P0, IVP_Compact_Edge* P1,
                                                        IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P0, IVP_SYNAPSE_POINT, P1) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_Compact_Edge* const Edges[2] = { P0, P1 };            // local_5c / local_58
    IVP_Cache_Ledge_Point* const Caches[2] = { Cache0, Cache1 }; // local_50 / local_4c

    IVP_U_Point World0; // local_48
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P0, Cache0, &World0);
    IVP_U_Point World1; // local_38
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P1, Cache1, &World1);

    // [0]: P1 in cache0's space (local_10), [1]: P0 in cache1's space (local_20); side i
    // compares the OTHER point against its own vertex fan.
    IVP_U_Point PosOther[2];
    IVP_TransformWorldToSlotSpace(Cache1->cache_object, &World0, &PosOther[1]);
    IVP_TransformWorldToSlotSpace(Cache0->cache_object, &World1, &PosOther[0]);

    const double Dx = (double)(World0.k[0]) - World1.k[0];
    const double Dy = (double)(World0.k[1]) - World1.k[1];
    const double Dz = (double)(World0.k[2]) - World1.k[2];
    const float QuadLength = (float)((Dz * Dz + Dy * Dy) + Dx * Dx);
    if (!(QuadLength > 1.0e-6f)) // DAT_063a5610
        return IVP_MRC_ENDLESS_LOOP;

    IVP_Ledge_Mindist* const Mindist = mindist;
    const double InvLength = 1.0 / std::sqrt((double)(QuadLength)); // FSQRT; FDIVR 1.0f
    Mindist->SeparationGap = (float)(InvLength * QuadLength - Mindist->CombinedRadius);
    Mindist->SeparatingNormal.z = (float)(Dz * InvLength);
    Mindist->SeparatingNormal.x = (float)(Dx * InvLength);
    Mindist->SeparatingNormal.y = (float)(Dy * InvLength);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObject0 = Cache0->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObject1 = Cache1->cache_object;
        const double Sx = (double)(CacheObject0->Secondary.x) - CacheObject1->Secondary.x;
        const double Sy = (double)(CacheObject0->Secondary.y) - CacheObject1->Secondary.y;
        const double Sz = (double)(CacheObject0->Secondary.z) - CacheObject1->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    float MaxValue = 0.0f;                              // local_78
    IVP_Compact_Edge* BestEdge = nullptr;               // local_98
    IVP_U_Point* BestPos = nullptr;                     // local_94
    IVP_Compact_Edge* BestOtherEdge = nullptr;          // local_88
    IVP_Cache_Ledge_Point* BestCache = nullptr;         // local_90
    IVP_Cache_Ledge_Point* BestOtherCache = nullptr;    // local_8c
    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Compact_Edge* const P = Edges[Side];
        IVP_Cache_Ledge_Point* const Cache = Caches[Side];
        IVP_U_Point* const Pos = &PosOther[Side];
        const IVP_U_Float_Point* const V = Cache->compact_poly_points + P->GetStartPointIndex();

        const float Vx = (float)((double)(Pos->k[0]) - V->X); // local_6c
        const float Vy = (float)((double)(Pos->k[1]) - V->Y); // local_68
        const float Vz = (float)((double)(Pos->k[2]) - V->Z); // local_64
        const float DotV = (float)(((double)(Vx) * V->X + (double)(Vy) * V->Y) +
                                              (double)(Vz) * V->Z); // local_54

        IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
        IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
        for (;;)
        {
            const IVP_U_Float_Point* const Q = Cache->compact_poly_points + Neighbour->GetStartPointIndex();
            const double ProjectionExtended = (((double)(Vx) * Q->X + (double)(Vy) * Q->Y) +
                                               (double)(Vz) * Q->Z) - DotV;
            const float Projection = (float)(ProjectionExtended); // local_70
            if (ProjectionExtended > 0.0)
            {
                const double Wx = (double)(Q->X) - V->X;
                const double Wy = (double)(Q->Y) - V->Y;
                const double Wz = (double)(Q->Z) - V->Z;
                const float QuadEdge = (float)((Wz * Wz + Wy * Wy) + Wx * Wx);
                const double Value = IVP_InvSqrtFloat(QuadEdge) * Projection;
                if (Value > MaxValue)
                {
                    MaxValue = (float)(Value);
                    BestOtherEdge = Edges[1 - Side];
                    BestOtherCache = Caches[1 - Side];
                    BestPos = Pos;
                    BestEdge = Neighbour;
                    BestCache = Cache;
                }
            }
            if (Neighbour == StopEdge)
                break;
            Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
        }
    }

    if (BestEdge != nullptr)
    {
        IVP_Unscaled_S_Result S; // local_28
        IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(BestCache->compact_ledge, BestEdge, BestPos, &S);
        if (S.Checks[1] > 0.0f)
        {
            if (!(S.Checks[0] >= 0.0f))
            {
                if (BestCache->synapse != Mindist->get_sorted_synapse(0))
                    Mindist->StateFlags ^= 0x100u;
                return p_minimize_PP(BestEdge, BestOtherEdge, BestCache, BestOtherCache);
            }
            if (BestOtherCache->synapse != Mindist->get_sorted_synapse(0))
                Mindist->StateFlags ^= 0x100u;
            return p_minimize_Leave_PK(BestOtherEdge, BestEdge, BestOtherCache, BestCache);
        }
    }

    Cache0->synapse->edge = P0;
    Cache0->synapse->status = IVP_SYNAPSE_POINT;
    Cache1->synapse->status = IVP_SYNAPSE_POINT;
    Cache1->synapse->edge = P1;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06365280 (.text:06365280-063658e8, thiscall, RET 0x10) (P, K, &cacheP, &cacheK).
//   :06365286-:063652c3 if (--P_Finish_Counter < 0) and check_loop_hash(0, P, 1, K) -> return 2.
//   :063652c6-:063652dc pos = FUN_0635ae60(P, cacheP, cacheK) (P in K's space, local_d8).
//   :063652e1-:063653fd in cacheK's points, K0 = start(K), K1 = start(next K), R = start(prev K),
//     S = start(prev(opposite K)); e = float(K1 - K0), d = float(pos - K0), a = float(R - K0),
//     b = float(S - K0) (each y, z, x).
//   :063653ff-:06365418 nK = e x a, nO = b x e (FUN_0628bed0).
//   :0636541d-:06365477 hK = float((dz*nKz + dy*nKy) + dx*nKx), hO = float((dz*nOz + dy*nOy) + nOx*dx).
//   :0636547b-:0636549c qrK = FUN_0635b210(cacheK ledge, K, pos), qrO = FUN_0635b210(.., opposite K, ..).
//   :063654a1-:0636553a qrK0 > 0: (qrO0 > 0 and hO > 0) ? PF(P, opposite K) : PF(P, K);
//     else qrO0 > 0: PF(P, opposite K) (all TEST AH,0x41 = "greater").
//   :0636553c-:063655ac hK < 0 and hO < 0 (TEST AH,1; unordered too): cacheP synapse = {P, 0},
//     cacheK synapse = {K, 5}, BacksidePosition = pos, return 3.
//   :063655af-:06365617 c = e x d; ie = float(1 / ((ex*ex + ez*ez) + ey*ey));
//     q = ((cz*cz + cy*cy) + cx*cx) * ie, FST float; q > 1e-10 (DAT_063a53d8, TEST AH,0x41):
//   :06365619-:06365694 i = float(06347df0(float q)); +0x54 = float(i*q - (+0x50)); c = e x c;
//     c rotated to world in place (FUN_0634d9d0, cacheK); +0x60 = float(c * -(i*ie)).
//   :06365696-:063656d7 else +0x54 = -(+0x50); n = perpendicular(e) (FUN_063485e0), normized
//     (FUN_06347ef0); +0x60 = n -- left in K's object space, as retail.
//   :063656da-:06365718 +0x58 = float((dy*ny + dz*nz) + dx*nx), d = cacheP.+0x5c - cacheK.+0x5c.
//   :0636571b-:06365724 c rotated into P's space in place (FUN_0634d970, cacheP) -- in the
//     degenerate case that is the unrotated e x d.
//   :06365729-:06365834 t = float(float q * 1e-6f); P's vertex fan (as 06363620): w = Q - V
//     (stack); p = (wz*cz + wy*cy) + wx*cx, FST float; p > 0: val = 06347df0(float((wy*wy +
//     wz*wz) + wx*wx)) * float(p); val > t: t = float(val), best = Q.
//   :06365836-:0636588e best: t >= 0.003f (DAT_063a545c, TEST AH,1) -> KK; else
//     IVP_KK_Input(K, best, cacheK, cacheP) (0635b3b0) and FUN_0635b570: EAX != 0 and
//     checks_L[0] >= 0 (TEST AH,1) -> KK; else :063658b5.
//   :06365890-:063658b2 06363ab0(best, K, cacheP, cacheK).
//   :063658b5-:063658e8 cacheP synapse = {P, 0}, cacheK synapse = {K, 1}; return 1.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                                                              IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P, IVP_SYNAPSE_EDGE, K) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point PosP; // local_d8
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheK, &PosP);

    const IVP_U_Float_Point* const PointsK = CacheK->compact_poly_points;
    IVP_Compact_Edge* const OppositeK = K->GetOppositeEdge();
    const IVP_U_Float_Point* const K0 = PointsK + K->GetStartPointIndex();
    const IVP_U_Float_Point* const K1 = PointsK + K->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const R = PointsK + K->GetPrevEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const S = PointsK + OppositeK->GetPrevEdge()->GetStartPointIndex();

    IVP_U_Point EdgeVec; // local_f8
    EdgeVec.k[1] = (float)((double)(K1->Y) - K0->Y);
    EdgeVec.k[2] = (float)((double)(K1->Z) - K0->Z);
    EdgeVec.k[0] = (float)((double)(K1->X) - K0->X);
    IVP_U_Point Delta; // local_e8
    Delta.k[1] = (float)((double)(PosP.k[1]) - K0->Y);
    Delta.k[2] = (float)((double)(PosP.k[2]) - K0->Z);
    Delta.k[0] = (float)((double)(PosP.k[0]) - K0->X);
    IVP_U_Point ToThird; // local_a8
    ToThird.k[1] = (float)((double)(R->Y) - K0->Y);
    ToThird.k[2] = (float)((double)(R->Z) - K0->Z);
    ToThird.k[0] = (float)((double)(R->X) - K0->X);
    IVP_U_Point ToOpposite; // local_98
    ToOpposite.k[1] = (float)((double)(S->Y) - K0->Y);
    ToOpposite.k[2] = (float)((double)(S->Z) - K0->Z);
    ToOpposite.k[0] = (float)((double)(S->X) - K0->X);

    IVP_U_Point NormalK; // local_c8
    IVP_CalcCrossProduct(&NormalK, &EdgeVec, &ToThird);
    IVP_U_Point NormalOpposite; // local_b8
    IVP_CalcCrossProduct(&NormalOpposite, &ToOpposite, &EdgeVec);

    const float HeightK = (float)(
        ((double)(Delta.k[2]) * NormalK.k[2] + (double)(Delta.k[1]) * NormalK.k[1]) +
        (double)(Delta.k[0]) * NormalK.k[0]); // local_108
    const float HeightOpposite = (float)(
        ((double)(Delta.k[2]) * NormalOpposite.k[2] + (double)(Delta.k[1]) * NormalOpposite.k[1]) +
        (double)(NormalOpposite.k[0]) * Delta.k[0]); // local_104

    IVP_Unscaled_QR_Result QRK; // local_78
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, K, &PosP, &QRK);
    IVP_Unscaled_QR_Result QROpposite; // local_88
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, OppositeK, &PosP, &QROpposite);

    if (QRK.Checks[0] > 0.0f)
    {
        if (QROpposite.Checks[0] > 0.0f && HeightOpposite > 0.0f)
            return p_minimize_PF(P, OppositeK, CacheP, CacheK);
        return p_minimize_PF(P, K, CacheP, CacheK);
    }
    if (QROpposite.Checks[0] > 0.0f)
        return p_minimize_PF(P, OppositeK, CacheP, CacheK);

    if (!(HeightK >= 0.0f) && !(HeightOpposite >= 0.0f))
    {
        CacheP->synapse->edge = P;
        CacheP->synapse->status = IVP_SYNAPSE_POINT;
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_ST_BACKSIDE;
        BacksidePosition.k[0] = PosP.k[0];
        BacksidePosition.k[1] = PosP.k[1];
        BacksidePosition.k[2] = PosP.k[2];
        return IVP_MRC_BACKSIDE;
    }

    IVP_U_Point Perpendicular; // local_118
    IVP_CalcCrossProduct(&Perpendicular, &EdgeVec, &Delta);
    const float InvQuadEdge = (float)(
        1.0 / (((double)(EdgeVec.k[0]) * EdgeVec.k[0] + (double)(EdgeVec.k[2]) * EdgeVec.k[2]) +
               (double)(EdgeVec.k[1]) * EdgeVec.k[1])); // local_128
    const double QuadDistExtended =
        (((double)(Perpendicular.k[2]) * Perpendicular.k[2] + (double)(Perpendicular.k[1]) * Perpendicular.k[1]) +
         (double)(Perpendicular.k[0]) * Perpendicular.k[0]) *
        InvQuadEdge;
    const float QuadDist = (float)(QuadDistExtended); // local_120

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (QuadDistExtended > (double)(1.0e-10f)) // DAT_063a53d8
    {
        const float InvDist = (float)(IVP_InvSqrtFloat(QuadDist)); // local_11c
        Mindist->SeparationGap = (float)((double)(InvDist) * QuadDist - Mindist->CombinedRadius);
        IVP_CalcCrossProduct(&Perpendicular, &EdgeVec, &Perpendicular);
        IVP_RotateSlotToWorldSpace(CacheK->cache_object, &Perpendicular, &Perpendicular); // 0634d9d0, in place
        const double Scale = -((double)(InvDist) * InvQuadEdge);
        Mindist->SeparatingNormal.y = (float)(Perpendicular.k[1] * Scale);
        Mindist->SeparatingNormal.z = (float)(Perpendicular.k[2] * Scale);
        Mindist->SeparatingNormal.x = (float)(Scale * Perpendicular.k[0]);
    }
    else
    {
        Mindist->SeparationGap = -Mindist->CombinedRadius;
        IVP_U_Point Fallback; // local_108
        Fallback.set_arbitrary_perpendicular_to(&EdgeVec); // 063485e0
        Fallback.normize();                                // 06347ef0
        Mindist->SeparatingNormal.x = Fallback.k[0];
        Mindist->SeparatingNormal.y = Fallback.k[1];
        Mindist->SeparatingNormal.z = Fallback.k[2];
    }

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const double Sx = (double)(CacheObjectP->Secondary.x) - CacheObjectK->Secondary.x;
        const double Sy = (double)(CacheObjectP->Secondary.y) - CacheObjectK->Secondary.y;
        const double Sz = (double)(CacheObjectP->Secondary.z) - CacheObjectK->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    IVP_RotateWorldToSlotSpace(CacheP->cache_object, &Perpendicular, &Perpendicular); // 0634d970, in place

    float MaxValue = (float)((double)(QuadDist) * 1.0e-6f); // DAT_063a5610, local_120
    const IVP_U_Float_Point* const V = CacheP->compact_poly_points + P->GetStartPointIndex();
    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    IVP_Compact_Edge* BestEdge = nullptr; // local_11c
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double Wx = (double)(Q->X) - V->X;
        const double Wy = (double)(Q->Y) - V->Y;
        const double Wz = (double)(Q->Z) - V->Z;
        const double DotExtended = (Wz * Perpendicular.k[2] + Wy * Perpendicular.k[1]) + Wx * Perpendicular.k[0];
        const float Dot = (float)(DotExtended); // local_108
        if (DotExtended > 0.0)
        {
            const float QuadEdge = (float)((Wy * Wy + Wz * Wz) + Wx * Wx);
            const double Value = IVP_InvSqrtFloat(QuadEdge) * Dot;
            if (Value > MaxValue)
            {
                MaxValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    bool bTakeEdgeEdge = false;
    if (BestEdge != nullptr)
    {
        if (MaxValue >= 0.003f) // DAT_063a545c
        {
            bTakeEdgeEdge = true;
        }
        else
        {
            const IVP_KK_Input KKInput(K, BestEdge, CacheK, CacheP); // local_68
            IVP_Unscaled_KK_Result KKResult;                        // local_108
            if (IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &KKResult) != 0 &&
                KKResult.checks_L[0] >= 0.0f)
                bTakeEdgeEdge = true;
        }
    }
    if (bTakeEdgeEdge)
        return p_minimize_KK(BestEdge, K, CacheP, CacheK);

    CacheP->synapse->edge = P;
    CacheP->synapse->status = IVP_SYNAPSE_POINT;
    CacheK->synapse->edge = K;
    CacheK->synapse->status = IVP_SYNAPSE_EDGE;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363ab0 (.text:06363ab0-06363df2, thiscall, RET 0x10) (K, L, &cacheK, &cacheL).
//   :06363ad6-:06363af1 IVP_KK_Input in(K, L, cacheK, cacheL) (0635b3b0); FUN_0635b570(&in, &r).
//   :06363af6-:06363b8f no sign bit in (r.L0 | r.L1): r.K0 < 0 (TEST AH,1; unordered too) ->
//     063651c0(K, L, cacheK, cacheL); r.K1 < 0 -> 063651c0(next K, L, ..);
//     else 06363e00(K, L, &in, &r, cacheK, cacheL).
//   :06363b92-:06363c16 else no sign bit in (r.K1 | r.K0): sorted synapse 0 := cacheL's
//     (flags ^= 0x100 when not); r.L0 < 0 -> 063651c0(L, K, cacheL, cacheK), else
//     063651c0(next L, K, cacheL, cacheK).
//   :06363c19-:06363c8f both sides out: pk = r.K0 < r.K1 ? K : next K, pk' the other one;
//     pl = r.L0 < r.L1 ? L : next L, pl' the other one (FCOMP; TEST AH,1).
//   :06363c8f-:06363ce1 s = FUN_0635b150(cacheL ledge, L, FUN_0635ae60(pk, cacheK, cacheL));
//     no sign bit in (s1 | s0) -> 063651c0(pk, L, cacheK, cacheL).
//   :06363ce4-:06363d5a s' = FUN_0635b150(cacheK ledge, K, FUN_0635ae60(pl, cacheL, cacheK));
//     no sign bit in (s'1 | s'0) -> sorted synapse 0 := cacheL's; 063651c0(pl, K, cacheL, cacheK).
//   :06363d5d-:06363df2 s0 * r.L0 < 0 (TEST AH,1) -> 06364890(pk, pl', cacheK, cacheL);
//     s'0 * r.K0 < 0 -> sorted synapse 0 := cacheL's, 06364890(pl, pk', cacheL, cacheK);
//     else 06364890(pk, pl, cacheK, cacheL).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L,
                                                        IVP_Cache_Ledge_Point* CacheK, IVP_Cache_Ledge_Point* CacheL)
{
    const IVP_KK_Input KKInput(K, L, CacheK, CacheL); // local_68
    IVP_Unscaled_KK_Result Result;                   // local_b4
    IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &Result);

    if (((IVP_MinimizeFloatBits(Result.checks_L[0]) | IVP_MinimizeFloatBits(Result.checks_L[1])) & 0x80000000u) == 0)
    {
        if (!(Result.checks_K[0] >= 0.0f))
            return p_minimize_PK(K, L, CacheK, CacheL);
        if (!(Result.checks_K[1] >= 0.0f))
            return p_minimize_PK(K->GetNextEdge(), L, CacheK, CacheL);
        return p_minimize_Leave_KK(K, L, &KKInput, &Result, CacheK, CacheL);
    }

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (((IVP_MinimizeFloatBits(Result.checks_K[1]) | IVP_MinimizeFloatBits(Result.checks_K[0])) & 0x80000000u) == 0)
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        if (!(Result.checks_L[0] >= 0.0f))
            return p_minimize_PK(L, K, CacheL, CacheK);
        return p_minimize_PK(L->GetNextEdge(), K, CacheL, CacheK);
    }

    IVP_Compact_Edge* PointK;      // local_b8
    IVP_Compact_Edge* OtherPointK; // local_a0
    if (!(Result.checks_K[0] >= Result.checks_K[1]))
    {
        PointK = K;
        OtherPointK = K->GetNextEdge();
    }
    else
    {
        PointK = K->GetNextEdge();
        OtherPointK = K;
    }
    IVP_Compact_Edge* PointL;      // local_a4
    IVP_Compact_Edge* OtherPointL; // local_9c
    if (!(Result.checks_L[0] >= Result.checks_L[1]))
    {
        PointL = L;
        OtherPointL = L->GetNextEdge();
    }
    else
    {
        PointL = L->GetNextEdge();
        OtherPointL = L;
    }

    IVP_U_Point PosKInL; // local_88
    IVP_Compact_Ledge_Solver::calc_pos_other_space(PointK, CacheK, CacheL, &PosKInL);
    IVP_Unscaled_S_Result SK; // local_98
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheL->compact_ledge, L, &PosKInL, &SK);
    if (((IVP_MinimizeFloatBits(SK.Checks[1]) | IVP_MinimizeFloatBits(SK.Checks[0])) & 0x80000000u) == 0)
        return p_minimize_PK(PointK, L, CacheK, CacheL);

    IVP_U_Point PosLInK; // local_78
    IVP_Compact_Ledge_Solver::calc_pos_other_space(PointL, CacheL, CacheK, &PosLInK);
    IVP_Unscaled_S_Result SL; // local_90
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &PosLInK, &SL);
    if (((IVP_MinimizeFloatBits(SL.Checks[1]) | IVP_MinimizeFloatBits(SL.Checks[0])) & 0x80000000u) == 0)
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        return p_minimize_PK(PointL, K, CacheL, CacheK);
    }

    if (!((double)(SK.Checks[0]) * Result.checks_L[0] >= 0.0))
        return p_minimize_PP(PointK, OtherPointL, CacheK, CacheL);
    if (!((double)(SL.Checks[0]) * Result.checks_K[0] >= 0.0))
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        return p_minimize_PP(PointL, OtherPointK, CacheL, CacheK);
    }
    return p_minimize_PP(PointK, PointL, CacheK, CacheL);
}

// ---------------------------------------------------------------------------
// FUN_06363e00 (.text:06363e00-063645c8, thiscall, RET 0x18) (K, L, &in, &r, &cacheK, &cacheL):
// the closest points lie inside both edges.
//   :06363e06-:06363e43 if (--P_Finish_Counter < 0) and check_loop_hash(1, K, 1, L) -> return 2.
//   :06363e46-:06363e90 d = in.L_Los[0] - in.K_Los[0] (stack); dist = float((dy*cy + dz*cz) + dx*cx),
//     c = in.cross_KL_Los; sign = sign bit of dist.
//   :06363e94-:06363ec6 inv = float(1 / sqrt((cx*cx + cy*cy) + cz*cz)).
//   :06363eca-:06363f04 n_world = FUN_0634d9d0(cacheL, float copy of c); n_K = FUN_0634d970(cacheK, n_world).
//   :06363f09-:06363f24 +0x54 = float(|inv*dist|); +0x54 = float(+0x54 - (+0x50)).
//   :06363f27-:06363f6d s = ((int)sign - 0.5f) * inv, doubled; +0x60 = float(n_world * s).
//   :06363f67-:06363fa5 +0x58 = float((dy*ny + dz*nz) + dx*nx), d = cacheK.+0x5c - cacheL.+0x5c.
//   :06363fa8-:06363fbe L0 / L1 carried into K's space (FUN_0635b030).
//   :06363fc3-:063640ec four 0x28-byte records at local_a0:
//     {triangle edge, point edge, point (in the triangle's space), point cache, triangle cache,
//      normal (in the triangle's space), hesse}:
//     0 {opposite K, L, L0 in K, cacheL, cacheK, n_K}, 1 {K, opposite L, L1 in K, cacheL, cacheK, n_K},
//     2 {opposite L, K, K_Los[0], cacheK, cacheL, c}, 3 {L, opposite K, K_Los[1], cacheK, cacheL, c}.
//   :063640f0-:06364158 hesse_i = FUN_0635b940(triangle_i, its ledge); flag_i = sign bit of
//     float((hz*nz + ny*hy) + nx*hx) ^ (i >> 1) ^ sign.
//   :0636415a-:06364317 max = -4e-6f (0xb68637bd); for j = 0..3: side = j ^ sign,
//     moving = record[side ^ flag_j]; v = point[side] - point[side ^ 1] (x, y float, z kept
//     extended for the dot); dot = (vz*hz + vx*hx) + vy*hy of hesse_j, FST float; dot < 0
//     (TEST AH,1; unordered too): val = (06347df0(float((vx^2 + vz^2) + vy^2)) * dot) *
//     float(06347df0(float((hx^2 + hz^2) + hy^2))), FST float; val < max (TEST AH,1):
//     qr = FUN_0635b210(triangle cache_j ledge, triangle_j, moving point); qr0 > 0 (TEST AH,0x41):
//     max = float(val), best = {moving point edge, moving point cache, triangle_j, its cache, qr}.
//   :0636431d-:0636442f no best: synapses {K, 1} / {L, 1}; flag0 + flag1 == 2: {K, 5} / {L, 2},
//     BacksidePosition = L0K (1 - t) + L1K t, t = float(r.L0 / (r.L0 + r.L1)), return 3;
//     flag2 + flag3 == 2: {K, 2} / {L, 5}, BacksidePosition from K_Los with r.K0 / r.K1, return 3;
//     else return 1.
//   :06364432-:063645c8 best: sorted synapse 0 := the point cache's; no sign bit in qr1|qr2|qr0 ->
//     06363620(P, FUN_0635ae60(P, ..), F, ..); qr2 >= 0 (TEST AH,1) -> 06363ab0(P, next F, ..);
//     qr1 >= 0 -> 06363ab0(P, prev F, ..); else d1 = float(0635bdd0(P, next F)),
//     d2 = 0635bdd0(P, opposite(prev F)); d2 < d1 (TEST AH,1; unordered too) -> 06363ab0(P, prev F),
//     else 06363ab0(P, next F).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L,
                                                              const IVP_KK_Input* KKInput,
                                                              const IVP_Unscaled_KK_Result* Result,
                                                              IVP_Cache_Ledge_Point* CacheK, IVP_Cache_Ledge_Point* CacheL)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_EDGE, K, IVP_SYNAPSE_EDGE, L) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    const IVP_U_Point* const Cross = &KKInput->cross_KL_Los;
    const IVP_U_Float_Point* const L0 = KKInput->L_Los[0];
    const IVP_U_Point* const KStart = &KKInput->K_Los[0]; // local_140
    const double Dx = (double)(L0->X) - KStart->k[0];
    const double Dy = (double)(L0->Y) - KStart->k[1];
    const double Dz = (double)(L0->Z) - KStart->k[2];
    const float Dist = (float)((Dy * Cross->k[1] + Dz * Cross->k[2]) + Dx * Cross->k[0]); // local_160
    const int32_t DistSign = static_cast<int32_t>(IVP_MinimizeFloatBits(Dist) >> 31);               // EBP
    const float InvLength = (float)(
        1.0 / std::sqrt(((double)(Cross->k[0]) * Cross->k[0] + (double)(Cross->k[1]) * Cross->k[1]) +
                        (double)(Cross->k[2]) * Cross->k[2])); // local_164

    IVP_U_Point CrossCopy; // local_100
    CrossCopy.k[0] = Cross->k[0];
    CrossCopy.k[1] = Cross->k[1];
    CrossCopy.k[2] = Cross->k[2];
    IVP_U_Point NormalWorld; // local_e0
    IVP_RotateSlotToWorldSpace(CacheL->cache_object, &CrossCopy, &NormalWorld); // 0634d9d0
    IVP_U_Point NormalK; // local_d0
    IVP_RotateWorldToSlotSpace(CacheK->cache_object, &NormalWorld, &NormalK); // 0634d970

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparationGap = (float)(std::fabs((double)(InvLength) * Dist));
    Mindist->SeparationGap = (float)((double)(Mindist->SeparationGap) - Mindist->CombinedRadius);
    double Scale = ((double)(DistSign) - 0.5f) * InvLength; // FILD; FSUB DAT_063a5460; FMUL
    Scale = Scale + Scale;
    Mindist->SeparatingNormal.z = (float)(NormalWorld.k[2] * Scale);
    Mindist->SeparatingNormal.x = (float)(NormalWorld.k[0] * Scale);
    Mindist->SeparatingNormal.y = (float)(NormalWorld.k[1] * Scale);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectL = CacheL->cache_object;
        const double Sx = (double)(CacheObjectK->Secondary.x) - CacheObjectL->Secondary.x;
        const double Sy = (double)(CacheObjectK->Secondary.y) - CacheObjectL->Secondary.y;
        const double Sz = (double)(CacheObjectK->Secondary.z) - CacheObjectL->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    IVP_U_Point L0InK; // local_c0
    IVP_Compact_Ledge_Solver::transform_pos_other_space(KKInput->L_Los[0], CacheL, CacheK, &L0InK);
    IVP_U_Point L1InK; // local_b0
    IVP_Compact_Ledge_Solver::transform_pos_other_space(KKInput->L_Los[1], CacheL, CacheK, &L1InK);

    // The four stack records (local_a0 + 0x28 * i).
    struct FLeaveKKRecord
    {
        IVP_Compact_Edge* Triangle;            // +0x00
        IVP_Compact_Edge* PointEdge;           // +0x04
        const IVP_U_Point* Point;              // +0x08
        IVP_Cache_Ledge_Point* CachePoint;     // +0x0c
        IVP_Cache_Ledge_Point* CacheTriangle;  // +0x10
        const IVP_U_Point* Normal;             // +0x14
        IVP_U_Point Hesse;                     // +0x18
    };
    IVP_Compact_Edge* const InK = const_cast<IVP_Compact_Edge*>(KKInput->K);
    IVP_Compact_Edge* const InL = const_cast<IVP_Compact_Edge*>(KKInput->L);
    FLeaveKKRecord Records[4];
    Records[0] = { K->GetOppositeEdge(), InL, &L0InK, CacheL, CacheK, &NormalK, {} };
    Records[1] = { K, InL->GetOppositeEdge(), &L1InK, CacheL, CacheK, &NormalK, {} };
    Records[2] = { L->GetOppositeEdge(), InK, &KKInput->K_Los[0], CacheK, CacheL, &CrossCopy, {} };
    Records[3] = { L, InK->GetOppositeEdge(), &KKInput->K_Los[1], CacheK, CacheL, &CrossCopy, {} };

    int32_t Flags[4]; // local_110
    for (int32_t I = 0; I <= 3; ++I)
    {
        FLeaveKKRecord& Record = Records[I];
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Record.Triangle, Record.Triangle->GetCompactLedge(),
                                                                    &Record.Hesse);
        const IVP_U_Point* const N = Record.Normal;
        const float Side = (float)(
            ((double)(Record.Hesse.k[2]) * N->k[2] + (double)(N->k[1]) * Record.Hesse.k[1]) +
            (double)(N->k[0]) * Record.Hesse.k[0]);
        Flags[I] = static_cast<int32_t>(IVP_MinimizeFloatBits(Side) >> 31) ^ (I >> 1) ^ DistSign;
    }

    float MaxValue = -4.0e-6f;                            // 0xb68637bd, local_13c
    IVP_Compact_Edge* BestPointEdge = nullptr;            // local_164
    IVP_Cache_Ledge_Point* BestCachePoint = nullptr;      // local_150
    IVP_Compact_Edge* BestTriangle = nullptr;             // local_158
    IVP_Cache_Ledge_Point* BestCacheTriangle = nullptr;   // local_15c
    IVP_Unscaled_QR_Result BestQR = {};                   // local_124
    for (int32_t J = 0; J <= 3; ++J)
    {
        const int32_t Side = J ^ DistSign;
        const FLeaveKKRecord& Moving = Records[Side ^ Flags[J]];
        const IVP_U_Point* const A = Records[Side].Point;
        const IVP_U_Point* const B = Records[Side ^ 1].Point;
        const IVP_U_Point& H = Records[J].Hesse;

        const float Vx = (float)((double)(A->k[0]) - B->k[0]); // local_134
        const float Vy = (float)((double)(A->k[1]) - B->k[1]); // local_130
        const double VzExtended = (double)(A->k[2]) - B->k[2];
        const float Vz = (float)(VzExtended);                             // local_12c (FST)
        const double DotExtended = (VzExtended * H.k[2] + (double)(Vx) * H.k[0]) + (double)(Vy) * H.k[1];
        const float Dot = (float)(DotExtended); // local_14c (FST)
        if (DotExtended >= 0.0)
            continue;

        const float InvLengthH = (float)(IVP_InvSqrtFloat((float)(
            ((double)(H.k[0]) * H.k[0] + (double)(H.k[2]) * H.k[2]) + (double)(H.k[1]) * H.k[1])));
        const double Value = (IVP_InvSqrtFloat((float)(
                                  ((double)(Vx) * Vx + (double)(Vz) * Vz) + (double)(Vy) * Vy)) *
                              Dot) *
                             InvLengthH;
        const float ValueRounded = (float)(Value); // FST local_14c
        if (Value >= MaxValue)
            continue;

        IVP_Unscaled_QR_Result QR; // local_f0
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Records[J].CacheTriangle->compact_ledge, Records[J].Triangle,
                                                               Moving.Point, &QR);
        if (!(QR.Checks[0] > 0.0f))
            continue;

        MaxValue = ValueRounded;
        BestPointEdge = Moving.PointEdge;
        BestCachePoint = Moving.CachePoint;
        BestTriangle = Records[J].Triangle;
        BestCacheTriangle = Records[J].CacheTriangle;
        BestQR = QR;
    }

    if (BestPointEdge == nullptr)
    {
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_SYNAPSE_EDGE;
        CacheL->synapse->status = IVP_SYNAPSE_EDGE;
        CacheL->synapse->edge = L;
        if (Flags[0] + Flags[1] == 2)
        {
            CacheK->synapse->edge = K;
            CacheK->synapse->status = IVP_ST_BACKSIDE;
            CacheL->synapse->status = IVP_SYNAPSE_POLY;
            CacheL->synapse->edge = L;
            const double CheckL0 = Result->checks_L[0];
            const float T = (float)(CheckL0 / (CheckL0 + Result->checks_L[1]));
            BacksidePosition.set_interpolate(&L0InK, &L1InK, T);
            return IVP_MRC_BACKSIDE;
        }
        if (Flags[2] + Flags[3] == 2)
        {
            CacheK->synapse->status = IVP_SYNAPSE_POLY;
            CacheK->synapse->edge = K;
            const double CheckK0 = Result->checks_K[0];
            const float T = (float)(CheckK0 / (CheckK0 + Result->checks_K[1]));
            BacksidePosition.set_interpolate(&KKInput->K_Los[0], &KKInput->K_Los[1], T);
            CacheL->synapse->edge = L;
            CacheL->synapse->status = IVP_ST_BACKSIDE;
            return IVP_MRC_BACKSIDE;
        }
        return IVP_MRC_OK;
    }

    if (BestCachePoint->synapse != Mindist->get_sorted_synapse(0))
        Mindist->StateFlags ^= 0x100u;

    if (((IVP_MinimizeFloatBits(BestQR.Checks[1]) | IVP_MinimizeFloatBits(BestQR.Checks[2]) |
          IVP_MinimizeFloatBits(BestQR.Checks[0])) & 0x80000000u) == 0)
    {
        IVP_U_Point PosInF; // local_110
        IVP_Compact_Ledge_Solver::calc_pos_other_space(BestPointEdge, BestCachePoint, BestCacheTriangle, &PosInF);
        return p_minimize_Leave_PF(BestPointEdge, &PosInF, BestTriangle, BestCachePoint, BestCacheTriangle);
    }
    if (BestQR.Checks[2] >= 0.0f)
        return p_minimize_KK(BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle);
    if (BestQR.Checks[1] >= 0.0f)
        return p_minimize_KK(BestPointEdge, BestTriangle->GetPrevEdge(), BestCachePoint, BestCacheTriangle);

    const float QuadDistNext = (float)(IVP_Compact_Ledge_Solver::calc_qlen_KK(
        BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle));
    const double QuadDistPrev = IVP_Compact_Ledge_Solver::calc_qlen_KK(
        BestPointEdge, BestTriangle->GetPrevEdge()->GetOppositeEdge(), BestCachePoint, BestCacheTriangle);
    if (!(QuadDistPrev >= QuadDistNext))
        return p_minimize_KK(BestPointEdge, BestTriangle->GetPrevEdge(), BestCachePoint, BestCacheTriangle);
    return p_minimize_KK(BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle);
}

// ---------------------------------------------------------------------------
// FUN_06352e40 (.text:06352e40-06353366, thiscall, RET 0x10) (F0, F1, &cache0, &cache1): the
// exhaustive feature search between two triangles; offsets re-read with capstone.
//   :06352e55 min = 1e20f (0x60ad78ec).
//   :06352e5d-:06352f24 vertex pairs (E0 over F0's triangle outer, E1 over F1's inner, next):
//     q = (dz*dz + dy*dy) + dx*dx of 0635bda0(E0, cache0) - 0635bda0(E1, cache1); FCOM min;
//     TEST AH,1 -> smaller (or unordered): min = float(q), synapses {E0, 0} / {E1, 0}.
//   :06352f2a-:06353060 points vs triangle, side 0 = F0's points vs F1, side 1 = F1's vs F0:
//     h = FUN_0635b8d0(tri edge, tri cache ledge); per point P: p = 0635ae60(P, point cache,
//     tri cache); no sign bit in FUN_0635b210(tri ledge, tri edge, p): d = ((hz*pz + hy*py) +
//     px*hx) + hw, q = d*d; q * 1.000001f (DAT_063a5650) < min (TEST AH,1): min = float(q),
//     synapses {P, 0} / {tri edge, 2}.
//   :06353066-:0635315c points vs edges, side 0 = F1's points vs F0's edges, side 1 the reverse:
//     per point (outer) p = 0635ae60(point, its cache, edge cache); per edge (inner) no sign bit
//     in FUN_0635b150(edge ledge, edge, p): q = FUN_0635ba10(edge ledge, edge, p); q * 1.000001f
//     < min: min = float(q), synapses {edge, 1} / {point, 0}.
//   :06353162-:0635323c edge pairs (K over F0 outer, L over F1 inner): IVP_KK_Input(K, L, cache0,
//     cache1), FUN_0635b570 (EAX unused); no sign bit in (K1|K0) nor in (L0|L1): q = FUN_0635b4d0;
//     q * 1.000001f < min: min = float(q), synapses {K, 1} / {L, 1}.
//   :06353242-:06353366 first = cache0, second = cache1, swapped when cache1's synapse status is 0
//     and cache0's is not; sorted synapse 0 := first's (flags ^= 0x100 when not); by first /
//     second status: 0/0 PP, 0/1 PK, 0/2 PF, 1/1 KK (first edge, second edge, first cache,
//     second cache); anything else CORE (`MOV [0]`) and return 0.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_FF(IVP_Compact_Edge* F0, IVP_Compact_Edge* F1,
                                                        IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1)
{
    float MinQuadDist = MinimizeSearchStart; // local_c8

    {
        IVP_Compact_Edge* E0 = F0;
        IVP_Compact_Edge* E1 = F1;
        for (int32_t I = 3; I != 0; --I)
        {
            for (int32_t J = 3; J != 0; --J)
            {
                IVP_U_Point World1; // local_ac
                IVP_Compact_Ledge_Solver::give_world_coords_AT(E1, Cache1, &World1);
                IVP_U_Point World0; // local_bc
                IVP_Compact_Ledge_Solver::give_world_coords_AT(E0, Cache0, &World0);
                const double Dx = (double)(World0.k[0]) - World1.k[0];
                const double Dy = (double)(World0.k[1]) - World1.k[1];
                const double Dz = (double)(World0.k[2]) - World1.k[2];
                const double QuadDist = (Dz * Dz + Dy * Dy) + Dx * Dx;
                if (!(QuadDist >= MinQuadDist))
                {
                    MinQuadDist = (float)(QuadDist);
                    Cache0->synapse->edge = E0;
                    Cache0->synapse->status = IVP_SYNAPSE_POINT;
                    Cache1->synapse->edge = E1;
                    Cache1->synapse->status = IVP_SYNAPSE_POINT;
                }
                E1 = E1->GetNextEdge();
            }
            E0 = E0->GetNextEdge();
        }
    }

    IVP_Cache_Ledge_Point* const TriangleCaches[2] = { Cache1, Cache0 }; // local_90
    IVP_Cache_Ledge_Point* const PointCaches[2] = { Cache0, Cache1 };    // local_88
    IVP_Compact_Edge* const PointEdges[2] = { F0, F1 };                  // local_80
    IVP_Compact_Edge* const TriangleEdges[2] = { F1, F0 };               // local_9c

    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Cache_Ledge_Point* const TriangleCache = TriangleCaches[Side];
        IVP_Compact_Edge* const Triangle = TriangleEdges[Side];
        IVP_U_Point Hesse; // local_bc
        IVP_Compact_Ledge_Solver::calc_hesse_object(Triangle, TriangleCache->compact_ledge, &Hesse);
        IVP_Compact_Edge* Point = PointEdges[Side];
        IVP_Cache_Ledge_Point* const PointCache = PointCaches[Side];
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_U_Point Pos; // local_ac
            IVP_Compact_Ledge_Solver::calc_pos_other_space(Point, PointCache, TriangleCache, &Pos);
            IVP_Unscaled_QR_Result QR; // local_78
            IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(TriangleCache->compact_ledge, Triangle, &Pos, &QR);
            if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) |
                  IVP_MinimizeFloatBits(QR.Checks[0])) & 0x80000000u) == 0)
            {
                const double Dist = (((double)(Hesse.k[2]) * Pos.k[2] + (double)(Hesse.k[1]) * Pos.k[1]) +
                                     (double)(Pos.k[0]) * Hesse.k[0]) +
                                    Hesse.k[3];
                const double QuadDist = Dist * Dist;
                if (!(QuadDist * 1.000001f >= MinQuadDist)) // DAT_063a5650
                {
                    MinQuadDist = (float)(QuadDist);
                    PointCache->synapse->edge = Point;
                    PointCache->synapse->status = IVP_SYNAPSE_POINT;
                    TriangleCache->synapse->edge = Triangle;
                    TriangleCache->synapse->status = IVP_SYNAPSE_POLY;
                }
            }
            Point = Point->GetNextEdge();
        }
    }

    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Compact_Edge* Point = TriangleEdges[Side];                   // EDI
        IVP_Cache_Ledge_Point* const PointCache = TriangleCaches[Side];  // EBP
        IVP_Cache_Ledge_Point* const EdgeCache = PointCaches[Side];      // EBX
        IVP_Compact_Edge* const FirstEdge = PointEdges[Side];            // local_94
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_U_Point Pos; // local_78
            IVP_Compact_Ledge_Solver::calc_pos_other_space(Point, PointCache, EdgeCache, &Pos);
            IVP_Compact_Edge* Edge = FirstEdge;
            for (int32_t J = 3; J != 0; --J)
            {
                IVP_Unscaled_S_Result S; // local_ac
                IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(EdgeCache->compact_ledge, Edge, &Pos, &S);
                if (((IVP_MinimizeFloatBits(S.Checks[1]) | IVP_MinimizeFloatBits(S.Checks[0])) & 0x80000000u) == 0)
                {
                    const double QuadDist =
                        IVP_Compact_Ledge_Solver::quad_dist_edge_to_point_K_space(EdgeCache->compact_ledge, Edge, &Pos);
                    if (!(QuadDist * 1.000001f >= MinQuadDist))
                    {
                        MinQuadDist = (float)(QuadDist);
                        EdgeCache->synapse->edge = Edge;
                        EdgeCache->synapse->status = IVP_SYNAPSE_EDGE;
                        PointCache->synapse->edge = Point;
                        PointCache->synapse->status = IVP_SYNAPSE_POINT;
                    }
                }
                Edge = Edge->GetNextEdge();
            }
            Point = Point->GetNextEdge();
        }
    }

    {
        IVP_Compact_Edge* K = F0;
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_Compact_Edge* L = F1;
            for (int32_t J = 3; J != 0; --J)
            {
                const IVP_KK_Input KKInput(K, L, Cache0, Cache1); // local_68
                IVP_Unscaled_KK_Result Result;                   // local_bc
                IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &Result);
                if (((IVP_MinimizeFloatBits(Result.checks_K[1]) | IVP_MinimizeFloatBits(Result.checks_K[0])) & 0x80000000u) == 0 &&
                    ((IVP_MinimizeFloatBits(Result.checks_L[0]) | IVP_MinimizeFloatBits(Result.checks_L[1])) & 0x80000000u) == 0)
                {
                    const double QuadDist = KKInput.calc_quad_distance_edge_edge();
                    if (!(QuadDist * 1.000001f >= MinQuadDist))
                    {
                        MinQuadDist = (float)(QuadDist);
                        Cache0->synapse->edge = K;
                        Cache0->synapse->status = IVP_SYNAPSE_EDGE;
                        Cache1->synapse->edge = L;
                        Cache1->synapse->status = IVP_SYNAPSE_EDGE;
                    }
                }
                L = L->GetNextEdge();
            }
            K = K->GetNextEdge();
        }
    }

    IVP_Cache_Ledge_Point* FirstCache = Cache0;
    IVP_Cache_Ledge_Point* SecondCache = Cache1;
    if (Cache1->synapse->status == IVP_SYNAPSE_POINT && Cache0->synapse->status != IVP_SYNAPSE_POINT)
    {
        FirstCache = Cache1;
        SecondCache = Cache0;
    }
    IVP_Synapse_Real* const FirstSynapse = FirstCache->synapse;
    IVP_Synapse_Real* const SecondSynapse = SecondCache->synapse;

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (FirstCache->synapse != Mindist->get_sorted_synapse(0))
        Mindist->StateFlags ^= 0x100u;

    IVP_Compact_Edge* const FirstEdge = FirstSynapse->edge;
    IVP_Compact_Edge* const SecondEdge = SecondSynapse->edge;
    if (FirstSynapse->status == IVP_SYNAPSE_POINT)
    {
        if (SecondSynapse->status == IVP_SYNAPSE_POINT)
            return p_minimize_PP(FirstEdge, SecondEdge, FirstCache, SecondCache);
        if (SecondSynapse->status == IVP_SYNAPSE_EDGE)
            return p_minimize_PK(FirstEdge, SecondEdge, FirstCache, SecondCache);
        if (SecondSynapse->status == IVP_SYNAPSE_POLY)
            return p_minimize_PF(FirstEdge, SecondEdge, FirstCache, SecondCache);
        IVP_CoreTrap();
        return IVP_MRC_UNINITIALIZED;
    }
    if (FirstSynapse->status == IVP_SYNAPSE_EDGE)
    {
        if (SecondSynapse->status == IVP_SYNAPSE_EDGE)
            return p_minimize_KK(FirstEdge, SecondEdge, FirstCache, SecondCache);
        IVP_CoreTrap();
        return IVP_MRC_UNINITIALIZED;
    }
    IVP_CoreTrap();
    return IVP_MRC_UNINITIALIZED;
}

// ---------------------------------------------------------------------------
// FUN_063645d0 (.text:063645d0-06364885, thiscall, RET 0xc) (&ball, P, &cacheP).
//   :063645d7-:06364604 if (--P_Finish_Counter < 0) and check_loop_hash(3, 0, 0, P) -> return 2.
//   :06364607-:0636462d w = FUN_0635bda0(P, cacheP) (world); b = ball centre (ball cache
//     object +0x4c) in P's space (FUN_0634d860, cacheP).
//   :06364632-:0636469d n = float(centre - w); len = FUN_06347e90(&n) (normalises, returns the
//     length); +0x60 = n; +0x54 = float(len - (+0x50)).
//   :063646a0-:063646d3 +0x58 = float((dy*ny + dz*nz) + dx*nx), d = ball.+0x5c - cacheP.+0x5c.
//   :063646d6-:06364741 v = float(b - V) (V = start of P; z kept extended for the dot);
//     vV = float((vz*Vz + vx*Vx) + vy*Vy).
//   :06364745-:063647ea P's fan as 06363620; p = ((vx*Qx + vy*Qy) + vz*Qz) - vV, FST float;
//     p > 0 (TEST AH,0x41): val = 06347df0(float((wz*wz + wy*wy) + wx*wx)) * float(p);
//     val > max (from 0.0f): max = float(val), best = Q.
//   :063647ef-:0636486c best and s1 > 0 (FUN_0635b150 of b on best): s0 < 0 (TEST AH,1) ->
//     063645d0(ball, best, cacheP), else 06364ce0(ball, best, cacheP); otherwise
//     cacheP synapse = {P, 0}, return 1.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BP(IVP_Cache_Ball* Ball, IVP_Compact_Edge* P, IVP_Cache_Ledge_Point* CacheP)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_BALL, nullptr, IVP_SYNAPSE_POINT, P) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point WorldP; // local_10
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P, CacheP, &WorldP);
    const IVP_U_Point* const BallCenter = &Ball->cache_object->WorldTransform.vv;
    IVP_U_Point BallInP; // local_20
    IVP_TransformWorldToSlotSpace(CacheP->cache_object, BallCenter, &BallInP);

    IVP_U_Point Normal; // local_30
    Normal.k[1] = (float)((double)(BallCenter->k[1]) - WorldP.k[1]);
    Normal.k[2] = (float)((double)(BallCenter->k[2]) - WorldP.k[2]);
    Normal.k[0] = (float)((double)(BallCenter->k[0]) - WorldP.k[0]);
    const double Length = Normal.real_length_plus_normize(); // FUN_06347e90

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparatingNormal.z = Normal.k[2];
    Mindist->SeparatingNormal.x = Normal.k[0];
    Mindist->SeparatingNormal.y = Normal.k[1];
    Mindist->SeparationGap = (float)(Length - Mindist->CombinedRadius);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectP->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectP->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectP->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    const IVP_U_Float_Point* const V = CacheP->compact_poly_points + P->GetStartPointIndex();
    const float Vx = (float)((double)(BallInP.k[0]) - V->X); // local_40
    const float Vy = (float)((double)(BallInP.k[1]) - V->Y); // local_3c
    const double VzExtended = (double)(BallInP.k[2]) - V->Z;
    const float Vz = (float)(VzExtended); // local_38 (FST)
    const float DotV = (float)((VzExtended * V->Z + (double)(Vx) * V->X) + (double)(Vy) * V->Y); // local_50

    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    float MaxValue = 0.0f;                // local_58
    IVP_Compact_Edge* BestEdge = nullptr; // param_3 slot
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double ProjectionExtended =
            (((double)(Vx) * Q->X + (double)(Vy) * Q->Y) + (double)(Vz) * Q->Z) - DotV;
        const float Projection = (float)(ProjectionExtended); // local_4c
        if (ProjectionExtended > 0.0)
        {
            const double Wx = (double)(Q->X) - V->X;
            const double Wy = (double)(Q->Y) - V->Y;
            const double Wz = (double)(Q->Z) - V->Z;
            const float QuadEdge = (float)((Wz * Wz + Wy * Wy) + Wx * Wx);
            const double Value = IVP_InvSqrtFloat(QuadEdge) * Projection;
            if (Value > MaxValue)
            {
                MaxValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    if (BestEdge != nullptr)
    {
        IVP_Unscaled_S_Result S; // local_48
        IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheP->compact_ledge, BestEdge, &BallInP, &S);
        if (S.Checks[1] > 0.0f)
        {
            if (!(S.Checks[0] >= 0.0f))
                return p_minimize_BP(Ball, BestEdge, CacheP);
            return p_minimize_BK(Ball, BestEdge, CacheP);
        }
    }

    CacheP->synapse->edge = P;
    CacheP->synapse->status = IVP_SYNAPSE_POINT;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06364ce0 (.text:06364ce0-06364d87, thiscall, RET 0xc) (&ball, K, &cacheK).
//   b = ball centre in K's space (FUN_0634d860, cacheK); s = FUN_0635b150(cacheK ledge, K, b);
//   s0 < 0 (TEST AH,1; unordered too) -> 063645d0(ball, K, cacheK); s1 < 0 -> 063645d0(ball,
//   next K, cacheK); else 06364d90(ball, K, cacheK).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K, IVP_Cache_Ledge_Point* CacheK)
{
    IVP_U_Point BallInK; // local_10
    IVP_TransformWorldToSlotSpace(CacheK->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInK);
    IVP_Unscaled_S_Result S; // local_18
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &BallInK, &S);
    if (!(S.Checks[0] >= 0.0f))
        return p_minimize_BP(Ball, K, CacheK);
    if (!(S.Checks[1] >= 0.0f))
        return p_minimize_BP(Ball, K->GetNextEdge(), CacheK);
    return p_minimize_Leave_BK(Ball, K, CacheK);
}

// ---------------------------------------------------------------------------
// FUN_06364d90 (.text:06364d90-063651b4, thiscall, RET 0xc) (&ball, K, &cacheK): the ball
// centre projects onto the inside of edge K. Offsets re-read with capstone.
//   :06364d96-:06364dcb if (--P_Finish_Counter < 0) and check_loop_hash(3, 0, 1, K) -> return 2.
//   :06364dce-:06364deb b = ball centre in K's space (FUN_0634d860, cacheK).
//   :06364df0-:06364f0b K0 / K1 / R = start(prev K) / S = start(prev(opposite K)) from cacheK's
//     points; e = float(K1 - K0), d = float(b - K0), a = float(R - K0), c = float(S - K0);
//     nK = e x a, nO = c x e (FUN_0628bed0).
//   :06364f26-:06364f7d hK = float((dz*nKz + dy*nKy) + dx*nKx), hO = float((dz*nOz + dy*nOy) + nOx*dx).
//   :06364f81-:06365031 qrK / qrO = FUN_0635b210 for K / opposite K; qrK0 > 0: (qrO0 > 0 and
//     hO > 0) ? 06363430(ball, opposite K) : 06363430(ball, K); else qrO0 > 0: 06363430(ball,
//     opposite K) (TEST AH,0x41 = greater).
//   :06365031-:06365086 hK < -1e-10f and hO < -1e-10f (DAT_063a589c, TEST AH,1; unordered too):
//     BacksidePosition = b, cacheK synapse = {K, 5}, return 3.
//   :06365089-:0636510c p = e x d; ie = float(1 / ((ez*ez + ey*ey) + ex*ex)); q = float(((pz*pz +
//     py*py) + px*px) * ie); i = float(06347df0(q)); +0x54 = float(i*q - (+0x50)).
//   :0636510f-:06365165 p = e x p, rotated to world in place (FUN_0634d9d0, cacheK);
//     +0x60 = float(p * -(i*ie)).
//   :06365168-:063651b4 +0x58 = float((dy*ny + dz*nz) + dx*nx), d = ball.+0x5c - cacheK.+0x5c;
//     cacheK synapse = {K, 1}; return 1.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K,
                                                              IVP_Cache_Ledge_Point* CacheK)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_BALL, nullptr, IVP_SYNAPSE_EDGE, K) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point BallInK; // local_70
    IVP_TransformWorldToSlotSpace(CacheK->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInK);

    const IVP_U_Float_Point* const PointsK = CacheK->compact_poly_points;
    IVP_Compact_Edge* const OppositeK = K->GetOppositeEdge();
    const IVP_U_Float_Point* const K0 = PointsK + K->GetStartPointIndex();
    const IVP_U_Float_Point* const K1 = PointsK + K->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const R = PointsK + K->GetPrevEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const S = PointsK + OppositeK->GetPrevEdge()->GetStartPointIndex();

    IVP_U_Point EdgeVec; // local_98
    EdgeVec.k[1] = (float)((double)(K1->Y) - K0->Y);
    EdgeVec.k[2] = (float)((double)(K1->Z) - K0->Z);
    EdgeVec.k[0] = (float)((double)(K1->X) - K0->X);
    IVP_U_Point Delta; // local_80
    Delta.k[1] = (float)((double)(BallInK.k[1]) - K0->Y);
    Delta.k[2] = (float)((double)(BallInK.k[2]) - K0->Z);
    Delta.k[0] = (float)((double)(BallInK.k[0]) - K0->X);
    IVP_U_Point ToThird; // local_30
    ToThird.k[1] = (float)((double)(R->Y) - K0->Y);
    ToThird.k[2] = (float)((double)(R->Z) - K0->Z);
    ToThird.k[0] = (float)((double)(R->X) - K0->X);
    IVP_U_Point ToOpposite; // local_40
    ToOpposite.k[1] = (float)((double)(S->Y) - K0->Y);
    ToOpposite.k[2] = (float)((double)(S->Z) - K0->Z);
    ToOpposite.k[0] = (float)((double)(S->X) - K0->X);

    IVP_U_Point NormalK; // local_60
    IVP_CalcCrossProduct(&NormalK, &EdgeVec, &ToThird);
    IVP_U_Point NormalOpposite; // local_50
    IVP_CalcCrossProduct(&NormalOpposite, &ToOpposite, &EdgeVec);

    const float HeightK = (float)(
        ((double)(Delta.k[2]) * NormalK.k[2] + (double)(Delta.k[1]) * NormalK.k[1]) +
        (double)(Delta.k[0]) * NormalK.k[0]); // local_b4
    const float HeightOpposite = (float)(
        ((double)(Delta.k[2]) * NormalOpposite.k[2] + (double)(Delta.k[1]) * NormalOpposite.k[1]) +
        (double)(NormalOpposite.k[0]) * Delta.k[0]); // local_b0

    IVP_Unscaled_QR_Result QRK; // local_10
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, K, &BallInK, &QRK);
    IVP_Unscaled_QR_Result QROpposite; // local_20
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, OppositeK, &BallInK, &QROpposite);

    if (QRK.Checks[0] > 0.0f)
    {
        if (QROpposite.Checks[0] > 0.0f && HeightOpposite > 0.0f)
            return p_minimize_BF(Ball, OppositeK, CacheK);
        return p_minimize_BF(Ball, K, CacheK);
    }
    if (QROpposite.Checks[0] > 0.0f)
        return p_minimize_BF(Ball, OppositeK, CacheK);

    if (!(HeightK >= -1.0e-10f) && !(HeightOpposite >= -1.0e-10f)) // DAT_063a589c
    {
        BacksidePosition.k[0] = BallInK.k[0];
        BacksidePosition.k[1] = BallInK.k[1];
        BacksidePosition.k[2] = BallInK.k[2];
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_ST_BACKSIDE;
        return IVP_MRC_BACKSIDE;
    }

    IVP_U_Point Perpendicular; // local_a8
    IVP_CalcCrossProduct(&Perpendicular, &EdgeVec, &Delta);
    const float InvQuadEdge = (float)(
        1.0 / (((double)(EdgeVec.k[2]) * EdgeVec.k[2] + (double)(EdgeVec.k[1]) * EdgeVec.k[1]) +
               (double)(EdgeVec.k[0]) * EdgeVec.k[0])); // local_b4
    const float QuadDist = (float)(
        (((double)(Perpendicular.k[2]) * Perpendicular.k[2] + (double)(Perpendicular.k[1]) * Perpendicular.k[1]) +
         (double)(Perpendicular.k[0]) * Perpendicular.k[0]) *
        InvQuadEdge); // local_84
    const float InvDist = (float)(IVP_InvSqrtFloat(QuadDist)); // local_88

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparationGap = (float)((double)(InvDist) * QuadDist - Mindist->CombinedRadius);
    IVP_CalcCrossProduct(&Perpendicular, &EdgeVec, &Perpendicular);
    IVP_RotateSlotToWorldSpace(CacheK->cache_object, &Perpendicular, &Perpendicular); // 0634d9d0, in place
    const double Scale = -((double)(InvDist) * InvQuadEdge);
    Mindist->SeparatingNormal.y = (float)(Perpendicular.k[1] * Scale);
    Mindist->SeparatingNormal.z = (float)(Perpendicular.k[2] * Scale);
    Mindist->SeparatingNormal.x = (float)(Scale * Perpendicular.k[0]);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectK->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectK->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectK->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    CacheK->synapse->edge = K;
    CacheK->synapse->status = IVP_SYNAPSE_EDGE;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363430 (.text:06363430-0636361d, thiscall, RET 0xc) (&ball, F, &cacheF).
//   :06363438-:0636344f b = ball centre in F's space (FUN_0634d860, cacheF).
//   :06363454-:06363484 qr = FUN_0635b210(cacheF ledge, F, b); any sign bit in qr1|qr2|qr0 -> outside.
//   :0636348a-:063634e1 inside: h = FUN_0635b940(F, cacheF ledge), normized (06347ef0);
//     +0x60 = FUN_0634d9d0(cacheF, h) (world).
//   :063634e2-:06363526 dist = ((bz*hz + by*hy) + hx*bx) - ((hz*Vz + hy*Vy) + hx*Vx) (V = start F);
//     cacheF synapse = {F, 2}.
//   :06363528-:06363564 dist < 0 (TEST AH,1; unordered too): cacheF synapse = {F, 5},
//     BacksidePosition = b, return 3.
//   :06363567-:063635b3 +0x54 = float(dist - (+0x50)); +0x58 = float((dy*ny + dz*nz) + dx*nx),
//     d = ball.+0x5c - cacheF.+0x5c; return 1.
//   :063635b6-:0636361d outside: min = 1e20f over F's three edges of FUN_0635bd10(b, ledge, e)
//     (FCOM; TEST AH,1 -> smaller or unordered: min = float, best = e); 06364ce0(ball, best, cacheF).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BF(IVP_Cache_Ball* Ball, IVP_Compact_Edge* F, IVP_Cache_Ledge_Point* CacheF)
{
    IVP_U_Point BallInF; // local_30
    IVP_TransformWorldToSlotSpace(CacheF->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInF);
    IVP_Unscaled_QR_Result QR; // local_20
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &BallInF, &QR);

    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
    {
        const IVP_U_Float_Point* const V = CacheF->compact_poly_points + F->GetStartPointIndex();
        IVP_U_Point Hesse; // local_40
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &Hesse);
        Hesse.normize();
        IVP_U_Point HesseWorld; // local_10
        IVP_RotateSlotToWorldSpace(CacheF->cache_object, &Hesse, &HesseWorld); // 0634d9d0

        IVP_Ledge_Mindist* const Mindist = mindist;
        Mindist->SeparatingNormal.x = HesseWorld.k[0];
        Mindist->SeparatingNormal.y = HesseWorld.k[1];
        Mindist->SeparatingNormal.z = HesseWorld.k[2];

        const double DistBall = ((double)(BallInF.k[2]) * Hesse.k[2] + (double)(BallInF.k[1]) * Hesse.k[1]) +
                                (double)(Hesse.k[0]) * BallInF.k[0];
        const double DistV = ((double)(Hesse.k[2]) * V->Z + (double)(Hesse.k[1]) * V->Y) +
                             (double)(Hesse.k[0]) * V->X;
        CacheF->synapse->edge = F;
        CacheF->synapse->status = IVP_SYNAPSE_POLY;
        const double Dist = DistBall - DistV;
        if (!(Dist >= 0.0))
        {
            CacheF->synapse->edge = F;
            CacheF->synapse->status = IVP_ST_BACKSIDE;
            BacksidePosition.k[0] = BallInF.k[0];
            BacksidePosition.k[1] = BallInF.k[1];
            BacksidePosition.k[2] = BallInF.k[2];
            return IVP_MRC_BACKSIDE;
        }

        Mindist->SeparationGap = (float)(Dist - Mindist->CombinedRadius);
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectF = CacheF->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectF->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectF->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectF->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
        return IVP_MRC_OK;
    }

    float MinQuadDist = MinimizeSearchStart; // param_3 slot
    IVP_Compact_Edge* BestEdge = nullptr;    // param_2 slot
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 3; I != 0; --I)
    {
        const double QuadDist = IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(&BallInF, CacheF->compact_ledge, Edge);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            BestEdge = Edge;
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_BK(Ball, BestEdge, CacheF);
}
