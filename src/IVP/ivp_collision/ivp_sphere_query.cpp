// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_sphere_query.h"

#include <cmath>

#include "ivp_compact_surface.h"
#include "../ivp_physics/ivp_core.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_physics/ivp_ov_element.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_surman_polygon.h"

// Every distance test below keeps its differences, squares and sums on the x87 stack and only
// compares; per the project's x87 rule a value held on the FPU stack is computed in double.

namespace
{
    // DAT_0658ab48: the hit flag the ledge-tree walk sets (0635072b) and collect_objects_in_node
    // clears before the walk (06350977) and reads back after it (063509ac). Nothing else
    // references it.
    int32_t G_bLedgeTreeSphereHit = 0;

    // DAT_063a5460 (.rdata, raw bytes 00 00 00 3f): 0.5f.
    const float G_Half = 0.5f;

    // FUN_06350630 (.text:06350630-0635068a, cdecl): 1 when the sphere (P, RadiusP) touches the
    // sphere (Q, RadiusQ).
    //   0635063b-0635064b  dx = P.x - Q.x, dy = P.y - Q.y, dz = P.z - Q.z (the FST of dz into
    //                      local_8 is a dead store)
    //   0635064f-06350653  R = RadiusP + RadiusQ
    //   06350657-0635066b  D = (dz*dz + dy*dy) + dx*dx; FCOMPP R*R against D
    //   06350675-0635067a  TEST AH,0x1 (C0); JZ -> return 1. C0 is set when R*R < D or the compare
    //                      is unordered, so the fall-through (return 0) is "less or NaN".
    int32_t IVP_SphereTouchesSphere(const IVP_U_Point* P, float RadiusP, const IVP_U_Point* Q, float RadiusQ)
    {
        const double Dx = (double)(P->k[0]) - Q->k[0];
        const double Dy = (double)(P->k[1]) - Q->k[1];
        const double Dz = (double)(P->k[2]) - Q->k[2];
        const double R = (double)(RadiusP) + RadiusQ;
        const double DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        if (R * R >= DistSq)
        {
            return 1;
        }
        return 0;
    }

    // FUN_06350690 (.text:06350690-0635073c, cdecl): walks a compact ledge tree from Node and
    // sets G_bLedgeTreeSphereHit when the sphere (P, Radius) reaches a terminal node, or any
    // node once Depth reaches MaxDepth. The right son is followed in the loop, the left son
    // (Node + 1, +0x1c) by recursion.
    //   063506a3           ChildDepth (EDI) = Depth + 1
    //   063506a6-063506ce  dx = P.x - center.x; dy = P.y - center.y; dz = P.z - center.z
    //                      (center.y / center.z go through float stack slots -- the same values)
    //   063506d2-063506d6  R = node radius (+0x14) + Radius
    //   063506da-063506fd  D = (dz*dz + dy*dy) + dx*dx; C0 (R*R < D or unordered) -> return
    //   063506ff-0635070a  Depth >= MaxDepth (JGE), or offset_right_node == 0 -> flag = 1, return
    //   0635070c-06350726  recurse on the left son with ChildDepth; Node = right son; ++Depth,
    //                      ++ChildDepth; loop
    void IVP_LedgeTreeSphereWalk(const IVP_Compact_Ledgetree_Node* Node, const IVP_U_Point* P, float Radius,
                                 int32_t Depth, int32_t MaxDepth)
    {
        int32_t ChildDepth = Depth + 1;
        for (;;)
        {
            const double Dx = (double)(P->k[0]) - Node->center[0];
            const double Dy = (double)(P->k[1]) - Node->center[1];
            const double Dz = (double)(P->k[2]) - Node->center[2];
            const double R = (double)(Node->radius) + Radius;
            const double DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            if (!(R * R >= DistSq))
            {
                return;
            }
            if (Depth >= MaxDepth || Node->offset_right_node == 0)
            {
                G_bLedgeTreeSphereHit = 1;
                return;
            }
            IVP_LedgeTreeSphereWalk(Node->left_son(), P, Radius, ChildDepth, MaxDepth);
            Node = Node->right_son();
            ++Depth;
            ++ChildDepth;
        }
    }
}

// FUN_06350740:
//   06350743-0635075f  Center = Record->Center (y / x through FLD/FSTP float, z as a dword)
//   06350765-06350768  Radius = Record->Radius (dword)
//   06350774-0635077b  MaxLedgeTreeDepth = Record->MaxLedgeTreeDepth
//   06350777-0635078a  TreeManager = Environment->ov_tree_manager (+0x14); Root = its
//                      CurrentRoot (+0x178)
//   06350790-063507a9  HalfSize (local_10..local_8) = (Radius, Radius, Radius)
//   063507ad-063507cd  QueryCenter (local_20..local_18) = Center
//   063507d1           collect_objects_in_node(Root, &HalfSize, &QueryCenter) -- the last PUSH
//                      (Root) is the first argument, &local_10 the second, &local_20 the third.
void IVP_Sphere_Object_Query::collect_objects(const FIVP_Sphere_Query_Record* Record, IVP_Environment* Environment)
{
    Center.k[0] = Record->Center[0];
    Center.k[1] = Record->Center[1];
    Center.k[2] = Record->Center[2];
    Radius = Record->Radius;
    MaxLedgeTreeDepth = Record->MaxLedgeTreeDepth;
    TreeManager = Environment->ov_tree_manager;
    IVP_OV_Node* const Root = TreeManager->CurrentRoot;

    IVP_U_Point HalfSize;     // local_10; its fourth dword is never written
    HalfSize.k[0] = Radius;
    HalfSize.k[1] = Radius;
    HalfSize.k[2] = Radius;

    IVP_U_Point QueryCenter;  // local_20; its fourth dword is never written
    QueryCenter.k[0] = Center.k[0];
    QueryCenter.k[1] = Center.k[1];
    QueryCenter.k[2] = Center.k[2];

    collect_objects_in_node(Root, &HalfSize, &QueryCenter);
}

// FUN_063507e0:
//   063507ec-063507fd  TreeManager->node_cell_bounds(Node, &Origin (local_10), &Extent (local_3c))
//   06350802-06350838  Half = Extent * 0.5f kept on the stack; its float copies go to
//                      local_20/1c/18; NodeCenter (local_30/2c/28) = float(Half + Origin[i])
//   0635083c-0635088b  per axis: |NodeCenter[i] - QueryCenter[i]| against float(Half) +
//                      HalfSize[i]; TEST AH,0x1 (C0) JNZ -> return, i.e. leave when
//                      Half + HalfSize < |...| or unordered
//   06350891-06350a15  every element (Node+0x22 count, +0x24 elems): Object = element->
//                      real_object (+0x24);
//                        Delta = Center - core->MWorldFCore.vv (FUN_0628bea0, float stores);
//                        R = Radius + core->UpperLimitRadius (local_38 + local_34);
//                        D = (dz*dz + dy*dy) + dx*dx; C0 -> next element (06350a08);
//                        Slot = the inlined FUN_06353510 (ResolveNarrowPhaseCacheSlot: lazy
//                        0634d530, ++RefCount, conditional 0634d640);
//                        Local = FUN_0634d860(Slot, &Center) (world -> slot space);
//                        flag = 0 (06350977);
//                        object_type 2: walk the polygon's ledge tree (surface manager +0x80 ->
//                          +0x04 compact surface -> + offset_ledgetree_root) from depth 0 to
//                          MaxLedgeTreeDepth, result = the flag;
//                        object_type 3: result = FUN_06350630(&Local, Radius, &core->vv,
//                          core->UpperLimitRadius) -- Local is in slot space and core->vv in
//                          world space, exactly as retail passes them;
//                        any other type: no test, result skipped (063509b9 -> 06350a01);
//                        result != 0 -> Found.Add(Object) (grows through FUN_063441a0);
//                        --Slot->RefCount (06350a01, inline DEC).
//   06350a23-06350a4f  every child (Node+0x1a count, +0x1c elems): recurse with the same
//                      HalfSize / QueryCenter.
// No pointer is tested anywhere.
void IVP_Sphere_Object_Query::collect_objects_in_node(IVP_OV_Node* Node, const IVP_U_Point* HalfSize,
                                                       const IVP_U_Point* QueryCenter)
{
    float Origin[3];   // local_10..local_8
    float Extent;      // local_3c
    TreeManager->node_cell_bounds(Node, Origin, &Extent);

    const double Half = (double)(Extent) * G_Half;
    const float HalfF = (float)(Half);                          // local_20/1c/18
    const float NodeCenterX = (float)(Half + Origin[0]);        // local_30
    const float NodeCenterY = (float)(Half + Origin[1]);        // local_2c
    const float NodeCenterZ = (float)(Half + Origin[2]);        // local_28

    if (!((double)(HalfF) + HalfSize->k[0] >= fabs((double)(NodeCenterX) - QueryCenter->k[0])))
    {
        return;
    }
    if (!((double)(HalfF) + HalfSize->k[1] >= fabs((double)(NodeCenterY) - QueryCenter->k[1])))
    {
        return;
    }
    if (!((double)(HalfF) + HalfSize->k[2] >= fabs((double)(NodeCenterZ) - QueryCenter->k[2])))
    {
        return;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(Node->Elements.n_elems); ++i)
    {
        IVP_Real_Object* const Object = Node->Elements.elems[i]->real_object;
        IVP_Core* const Core = Object->core;
        const float ObjectRadius = Core->UpperLimitRadius;   // local_34
        const float QueryRadius = Radius;                    // local_38

        IVP_U_Point Delta;                                   // local_20..local_18
        Delta.subtract(&Center, &Core->MWorldFCore.vv);

        const double R = (double)(QueryRadius) + ObjectRadius;
        const double DistSq = ((double)(Delta.k[2]) * Delta.k[2] + (double)(Delta.k[1]) * Delta.k[1])
                            + (double)(Delta.k[0]) * Delta.k[0];
        if (!(R * R >= DistSq))
        {
            continue;
        }

        IVP_NarrowPhaseCacheSlot* const Slot = Object->ResolveNarrowPhaseCacheSlot();

        IVP_U_Point Local;                                   // local_30
        IVP_TransformWorldToSlotSpace(Slot, &Center, &Local);

        G_bLedgeTreeSphereHit = 0;
        const int32_t Type = Object->object_type;
        if (Type == 2)
        {
            const IVP_SurfaceManager_Polygon* const SurfaceManager =
                static_cast<const IVP_SurfaceManager_Polygon*>(Object->surface_manager);
            IVP_LedgeTreeSphereWalk(SurfaceManager->compact_surface->get_compact_ledge_tree_root(),
                                    &Local, Radius, 0, MaxLedgeTreeDepth);
            if (G_bLedgeTreeSphereHit != 0)
            {
                Found.Add(Object);
            }
        }
        else if (Type == 3)
        {
            if (IVP_SphereTouchesSphere(&Local, Radius, &Core->MWorldFCore.vv, Core->UpperLimitRadius) != 0)
            {
                Found.Add(Object);
            }
        }

        Object->ReleaseNarrowPhaseCacheSlot(Slot);
    }

    for (int32_t i = 0; i < static_cast<int32_t>(Node->Children.n_elems); ++i)
    {
        collect_objects_in_node(Node->Children.elems[i], HalfSize, QueryCenter);
    }
}
