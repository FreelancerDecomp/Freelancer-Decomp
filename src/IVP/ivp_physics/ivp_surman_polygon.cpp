// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_SurfaceManager_Polygon (ivp_surman_polygon.h). Traced against EXE\Disasm\Common.dll.txt
// .text:0634a9b0-0634ad66 with retail's x87 rounding points (stack values double, every
// `FSTP float ptr` a float).

#include "ivp_surman_polygon.h"

#include <cmath>

#include "../ivp_collision/ivp_compact_ledge_solver.h"
#include "../ivp_collision/ivp_compact_surface.h"
#include "../ivp_collision/ivp_ray_solver.h"
#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (FUN_0635d3e0)

namespace
{
    void IVP_SurManPolygonAppendLedge(IVP_SurfaceManager_OutList* OutList, const IVP_Compact_Ledge* Ledge)
    {
        // CMP [list+4],[list]; JL -> grow only when Count >= Capacity.
        if (!(OutList->Count < OutList->Capacity))
            IVP_U_BigVector_Grow(OutList); // FUN_0635d3e0
        OutList->Elems[OutList->Count] = const_cast<IVP_Compact_Ledge*>(Ledge);
        OutList->Count = OutList->Count + 1;
    }

    // The 0x20-byte stack object 0634ab90 builds and hands to FUN_0634a9b0 as `this`:
    // {observer - r (3 floats), observer + r (3 floats), 0, 0}. FUN_0634a9b0 only passes
    // `this` on to its own recursion and never reads a field.
    struct FIVP_LedgeRadiusQuery
    {
        float   Min[3];  // -0x20
        float   Max[3];  // -0x14
        int32_t Zero[2]; // -0x08 / -0x04

        void CollectLedgesWithinRadius(const IVP_Compact_Ledgetree_Node* Node, const float* Center, float Radius,
                                       IVP_SurfaceManager_OutList* OutList) const;
    };

    // FUN_0634a9b0 (thiscall, RET 0x10).
    //   d = node.center - observer (dx on the stack, dy/dz floats);
    //   reach = r + node.radius; FCOMPP(reach^2, (dz*dz + dy*dy) + dx*dx); TEST AH,0x1; JNZ miss;
    //   s = float(node.radius * 0.004f); per axis FCOMPP(box_sizes[k] * s + r, |d_k|);
    //   TEST AH,0x41; JNZ miss -> the box must strictly exceed |d_k|.
    //   A node with a ledge (terminal ledge or inner hull ledge) appends it and stops;
    //   otherwise recurse left, continue right. A terminal node without a ledge offset
    //   (:0634aafa) uses node + 0 as the ledge: two-triangle ledges must also pass
    //   r^2 >= calc_qlen_PF_F_space(...) (TEST AH,0x1; JNZ miss), then it is appended.
    void FIVP_LedgeRadiusQuery::CollectLedgesWithinRadius(const IVP_Compact_Ledgetree_Node* Node, const float* Center,
                                                          float Radius, IVP_SurfaceManager_OutList* OutList) const
    {
        for (;;)
        {
            const double Dx = (double)(Node->center[0]) - Center[0];
            const float Dy = (float)((double)(Node->center[1]) - Center[1]);
            const float Dz = (float)((double)(Node->center[2]) - Center[2]);
            const double Reach = (double)(Radius) + Node->radius;
            const double QuadDist = ((double)(Dz) * Dz + (double)(Dy) * Dy) + Dx * Dx;
            if (!(Reach * Reach >= QuadDist))
                return;

            const float Scale = (float)((double)(Node->radius) * (double)(0.004f)); // DAT_063a54b4
            if (!((double)(Node->box_sizes[0]) * Scale + Radius > std::fabs(Dx)))
                return;
            if (!((double)(Node->box_sizes[1]) * Scale + Radius > std::fabs((double)(Dy))))
                return;
            if (!((double)(Node->box_sizes[2]) * Scale + Radius > std::fabs((double)(Dz))))
                return;

            if (Node->offset_compact_ledge != 0)
            {
                const IVP_Compact_Ledge* Ledge = Node->get_compact_ledge();
                if (Ledge != nullptr)
                {
                    IVP_SurManPolygonAppendLedge(OutList, Ledge);
                    return;
                }
            }

            if (Node->offset_right_node == 0)
            {
                const IVP_Compact_Ledge* Ledge = Node->get_compact_ledge();
                if (Ledge->GetNTriangles() == 2)
                {
                    const double QuadFaceDist = IVP_Compact_Ledge_Solver::calc_qlen_PF_F_space(
                        Ledge, Ledge->GetFirstTriangle(), reinterpret_cast<const IVP_U_Point*>(Center));
                    if (!((double)(Radius) * Radius >= QuadFaceDist))
                        return;
                }
                IVP_SurManPolygonAppendLedge(OutList, Ledge);
                return;
            }

            CollectLedgesWithinRadius(Node->left_son(), Center, Radius, OutList);
            Node = Node->right_son();
        }
    }
}

IVP_SurfaceManager_Polygon::IVP_SurfaceManager_Polygon(const IVP_Compact_Surface* CompactSurface)
    : compact_surface(CompactSurface)
{
}

// 0634ad60: `MOV [ECX],0639c2dc; JMP 0635d440` -- nothing but the vptr restores.
IVP_SurfaceManager_Polygon::~IVP_SurfaceManager_Polygon()
{
}

// 0634ad40.
void* IVP_SurfaceManager_Polygon::get_single_convex()
{
    const IVP_Compact_Ledgetree_Node* Root = compact_surface->get_compact_ledge_tree_root();
    if (Root->offset_right_node == 0)
        return const_cast<IVP_Compact_Ledge*>(Root->get_compact_ledge());
    if (Root->offset_compact_ledge != 0)
        return const_cast<IVP_Compact_Ledge*>(Root->get_compact_ledge());
    return nullptr;
}

// 0634ac70: three dword copies of mass_center. Retail leaves mass_center.z's bits in EAX; no
// caller reads it, the port returns OutVec3.
float* IVP_SurfaceManager_Polygon::get_mass_center(float* OutVec3)
{
    OutVec3[0] = compact_surface->mass_center[0];
    OutVec3[1] = compact_surface->mass_center[1];
    OutVec3[2] = compact_surface->mass_center[2];
    return OutVec3;
}

// 0634acb0: d = sqrt((dz*dz + dy*dy) + dx*dx) with d = mass_center - center (stack);
// *radius = float(d + upper_limit_radius);
// *radius_dev = float(d + max_factor_surface_deviation * upper_limit_radius * 0.004f), the
// deviation byte loaded through FILD qword (high dword zeroed at :0634acd0).
void IVP_SurfaceManager_Polygon::get_radius_and_radius_dev_to_given_center(void* Param1, void* Param2, void* Param3)
{
    const IVP_U_Point* Center = static_cast<const IVP_U_Point*>(Param1);
    float* OutRadius = static_cast<float*>(Param2);
    float* OutRadiusDev = static_cast<float*>(Param3);

    const double Dx = (double)(compact_surface->mass_center[0]) - Center->k[0];
    const double Dy = (double)(compact_surface->mass_center[1]) - Center->k[1];
    const double Dz = (double)(compact_surface->mass_center[2]) - Center->k[2];
    const double Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);

    *OutRadius = (float)(Dist + compact_surface->upper_limit_radius);

    const int64_t Deviation = static_cast<int64_t>(compact_surface->max_deviation_and_size & 0xff);
    *OutRadiusDev = (float)((double)(Deviation) * compact_surface->upper_limit_radius *
                                       (double)(0.004f) + Dist);
}

// 0634ac90: three dword copies of rotation_inertia.
void IVP_SurfaceManager_Polygon::get_rotation_inertia(void* Param1)
{
    float* Out = static_cast<float*>(Param1);
    Out[0] = compact_surface->rotation_inertia[0];
    Out[1] = compact_surface->rotation_inertia[1];
    Out[2] = compact_surface->rotation_inertia[2];
}

// 0634ab90 (RET 0x18). Builds the observer +/- radius box (floats, never read) and walks the
// ledge tree: from the surface root when no root ledge is given, otherwise from the two sons
// of the root ledge's own tree node (ledge + ledge->LedgetreeNodeOffset).
void IVP_SurfaceManager_Polygon::get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                                              void* /*Param4*/, void* /*Param5*/,
                                                              IVP_SurfaceManager_OutList* OutList)
{
    FIVP_LedgeRadiusQuery Query;
    Query.Max[0] = (float)((double)(Radius) + Center3[0]);
    Query.Zero[0] = 0;
    Query.Zero[1] = 0;
    Query.Max[1] = (float)((double)(Radius) + Center3[1]);
    Query.Max[2] = (float)((double)(Radius) + Center3[2]);
    Query.Min[0] = (float)((double)(Center3[0]) - Radius);
    Query.Min[1] = (float)((double)(Center3[1]) - Radius);
    Query.Min[2] = (float)((double)(Center3[2]) - Radius);

    const IVP_Compact_Ledge* RootLedge = static_cast<const IVP_Compact_Ledge*>(Param3);
    if (RootLedge == nullptr)
    {
        Query.CollectLedgesWithinRadius(compact_surface->get_compact_ledge_tree_root(), Center3, Radius, OutList);
        return;
    }

    const IVP_Compact_Ledgetree_Node* RootNode = reinterpret_cast<const IVP_Compact_Ledgetree_Node*>(
        reinterpret_cast<const char*>(RootLedge) + RootLedge->LedgetreeNodeOffset);
    Query.CollectLedgesWithinRadius(RootNode->left_son(), Center3, Radius, OutList);
    Query.CollectLedgesWithinRadius(RootNode->right_son(), Center3, Radius, OutList);
}

// 0634ac50: FUN_0635abf0(compact_surface, list).
void IVP_SurfaceManager_Polygon::get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList)
{
    IVP_Compact_Ledge_Solver::get_all_ledges(compact_surface, OutList);
}

// 0634ab60 (RET 8): an IVP_Ray_Solver_Os on the stack (0634a500), then 0634a4e0 on the
// surface -- the non-virtual compact-surface entry.
void IVP_SurfaceManager_Polygon::insert_all_ledges_hitting_ray(void* Param1, void* Param2)
{
    IVP_Ray_Solver_Os RaySolverOs(static_cast<IVP_Ray_Solver*>(Param1), static_cast<IVP_Real_Object*>(Param2));
    RaySolverOs.check_ray_against_compact_surface(compact_surface);
}

// 06325390: XOR EAX,EAX; RET (IVP_SURMAN_POLYGON).
int32_t IVP_SurfaceManager_Polygon::get_type()
{
    return 0;
}
