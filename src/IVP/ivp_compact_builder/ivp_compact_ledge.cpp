// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_compact_ledge.h"

#include <cstdint>
#include <vector>

#include "../ivp_collision/ivp_compact_ledge_solver.h"

// Retail FUN_06353840 (.text:06353840-06353922), cdecl (start edge, position).
//  - Ledge = StartEdge's triangle - (tri_index + 1) * 16; a zeroed byte flag
//    per triangle (alloca of NTriangles rounded up to 4, FUN_06391dc0; the
//    port holds the flags in a std::vector).
//  - Edge = first edge (+0x14) of triangle pierce_index (TriInfo bits 12..23)
//    of StartEdge's triangle.
//  - loop: flag[Edge's triangle] = 1; FUN_0635b210(ledge, Edge, Position,
//    &w[4]) (IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space, raw
//    ledge); for K = 0..2 with C = Edge, Edge->next, ...: FLD w[K];
//    FCOMP 0.0f (DAT_063a53d4); TEST AH,0x41; JZ -> w[K] > 0: try the next
//    edge; otherwise the opposite edge's triangle flag decides: set -> next
//    edge, clear -> Edge = C->opposite and loop. After three blocked edges
//    the current Edge is returned.
IVP_Compact_Edge* IVP_WalkPiercingTriangleChain(IVP_Compact_Edge* StartEdge, const IVP_U_Point* Position)
{
    IVP_Compact_Ledge* Ledge = StartEdge->GetCompactTriangle()->GetCompactLedge();
    const uint32_t PierceIndex = StartEdge->GetCompactTriangle()->GetPierceIndex();

    std::vector<uint8_t> Visited(static_cast<size_t>(Ledge->GetNTriangles()), 0);

    IVP_Compact_Edge* Edge = Ledge->GetFirstTriangle()[PierceIndex].GetFirstEdge();
    for (;;)
    {
        Visited[Edge->GetCompactTriangle()->GetTriIndex()] = 1;

        IVP_Unscaled_QR_Result Weights;
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Ledge, Edge, Position, &Weights);

        IVP_Compact_Edge* Candidate = Edge;
        int32_t Count = 0;
        for (;;)
        {
            const bool bPositive = Weights.Checks[Count] > 0.0f;
            if (!bPositive && Visited[Candidate->GetOppositeEdge()->GetCompactTriangle()->GetTriIndex()] == 0)
                break;

            Candidate = Candidate->GetNextEdge();
            ++Count;
            if (Count >= 3)
                return Edge;
        }

        Edge = Candidate->GetOppositeEdge();
    }
}
