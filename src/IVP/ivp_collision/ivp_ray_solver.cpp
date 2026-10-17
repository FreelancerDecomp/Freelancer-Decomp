// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP ray casting -- IVP_Ray_Solver / IVP_Ray_Solver_Os (see ivp_ray_solver.h).
//
// Traced against EXE\Disasm\Common.dll.txt .text:06349a60-0634a9a8 with retail's x87
// rounding points: a value that stays on the FPU stack is double here, every
// `FSTP float ptr` is a float conversion. Comparisons follow the FNSTSW / TEST AH / Jcc
// pattern of each site, including which side an unordered (NaN) compare falls on.

#include "ivp_ray_solver.h"

#include <cstring>

#include "ivp_compact_ledge_solver.h"
#include "ivp_compact_surface.h"
#include "../ivp_physics/ivp_core.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_physics/ivp_ov_element.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_surface_manager.h"

namespace
{
    // FUN_0628bed0 (thiscall Out; A, B): Out = A x B with retail's term order
    // x = Ay*Bz - Az*By, y = Az*Bx - Ax*Bz, z = Ax*By - Ay*Bx (products on the stack).
    void IVP_RayCrossProduct(const IVP_U_Point* A, const IVP_U_Point* B, IVP_U_Point* Out)
    {
        const double Ax = A->k[0], Ay = A->k[1], Az = A->k[2];
        const double Bx = B->k[0], By = B->k[1], Bz = B->k[2];
        Out->k[0] = (float)(Ay * Bz - Az * By);
        Out->k[1] = (float)(Bx * Az - Ax * Bz);
        Out->k[2] = (float)(Ax * By - Ay * Bx);
    }

    // FUN_062c4780: (x*x + y*y) + z*z, left on the stack.
    double IVP_RayQuadLength(const IVP_U_Point* V)
    {
        return ((double)(V->k[0]) * V->k[0] + (double)(V->k[1]) * V->k[1]) +
               (double)(V->k[2]) * V->k[2];
    }

    // (n.z*p.z + n.y*p.y) + n.x*p.x -- the dot order every plane test in 06349fd0 uses.
    double IVP_RayDotZYX(const IVP_U_Point* N, double Px, double Py, double Pz)
    {
        return ((double)(N->k[2]) * Pz + (double)(N->k[1]) * Py) +
               (double)(N->k[0]) * Px;
    }

    uint32_t IVP_RayFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // FUN_0635d3e0 (IVP_U_BigVector_Grow, ivp_utility/ivp_u_bigvector.cpp): IVP_U_BigVector grow, shared with the surface
    // managers' ledge lists.
}

// ============================================================================
// IVP_Ray_Solver
// ============================================================================

// FUN_06349a60. Copies direction (+0x34), start (+0x04), length (+0x44) and flags (+0x48)
// from the template, then centre = start + (length * 0.5) * dir and
// end = start + length * dir, each component float(h * d + s) with h extended.
IVP_Ray_Solver::IVP_Ray_Solver(const IVP_Ray_Solver_Template* Template)
{
    ray_normized_direction.k[0] = Template->ray_normized_direction.k[0];
    ray_normized_direction.k[1] = Template->ray_normized_direction.k[1];
    ray_normized_direction.k[2] = Template->ray_normized_direction.k[2];
    ray_start_point.k[0] = Template->ray_start_point.k[0];
    ray_start_point.k[1] = Template->ray_start_point.k[1];
    ray_start_point.k[2] = Template->ray_start_point.k[2];
    ray_length = Template->ray_length;
    ray_flags = Template->ray_flags;

    // :06349aa3-06349aea: the centre starts as a copy of start, then gets h * dir added.
    const double Half = (double)(ray_length) * 0.5; // DAT_063a5460
    ray_center_point.k[0] = (float)(Half * ray_normized_direction.k[0] + ray_start_point.k[0]);
    ray_center_point.k[1] = (float)(Half * ray_normized_direction.k[1] + ray_start_point.k[1]);
    ray_center_point.k[2] = (float)(Half * ray_normized_direction.k[2] + ray_start_point.k[2]);

    // :06349aed-06349b16.
    const double Length = ray_length;
    ray_end_point.k[0] = (float)(Length * ray_normized_direction.k[0] + ray_start_point.k[0]);
    ray_end_point.k[1] = (float)(Length * ray_normized_direction.k[1] + ray_start_point.k[1]);
    ray_end_point.k[2] = (float)(Length * ray_normized_direction.k[2] + ray_start_point.k[2]);
}

// FUN_06349ba0. The two axis words form a 2-entry array that is walked from the second entry
// down (:06349bc5 ESI = &AxisB, :06349c05 SUB ESI,2). Per axis:
//   V = dir[k] * length * PlaneDist; Lo' = (Lo[k] - start[k]) * Scale;
//   FCOMPP(V, Lo'); TEST AH,0x1; JNZ -> return 0 when V < Lo' (or unordered);
//   Hi' = (Hi[k] - start[k]) * Scale; FCOMPP(V, Hi'); TEST AH,0x41; JZ -> return 0 when V > Hi'.
int IVP_Ray_Solver::check_ray_against_box_face(float PlaneDist, float Scale, const float* Lo, const float* Hi,
                                               int16_t AxisA, int16_t AxisB)
{
    const int16_t Axes[2] = { AxisA, AxisB };
    for (int Index = 1; Index >= 0; --Index)
    {
        const int K = Axes[Index];
        const double V = (double)(ray_normized_direction.k[K]) * ray_length * PlaneDist;
        const double LoV = ((double)(Lo[K]) - ray_start_point.k[K]) * Scale;
        if (!(V >= LoV))
            return 0;
        const double HiV = ((double)(Hi[K]) - ray_start_point.k[K]) * Scale;
        if (V > HiV)
            return 0;
    }
    return 1;
}

// FUN_06349c30. Cohen-Sutherland codes of start (+0x04) and end (+0x14) against [Min, Max]:
// per axis bit 1/4/0x10 when p < Min (FCOMP; TEST AH,0x1; JZ -> unordered also sets it),
// otherwise bit 2/8/0x20 when p > Max (TEST AH,0x41; JNZ skip). Either code 0 -> 1; common
// bit -> 0; then one box face per bit set in the start code and clear in the end code, in
// retail's order 0x10, 0x2, 0x20, 0x1, 0x4, 0x8. Every plane distance / scale is spilled
// as a float before the call.
int IVP_Ray_Solver::check_ray_against_box(const float* Min, const float* Max)
{
    int32_t StartCode = 0;
    if (!(ray_start_point.k[0] >= Min[0]))
        StartCode = 1;
    else if (ray_start_point.k[0] > Max[0])
        StartCode = 2;
    if (!(ray_start_point.k[1] >= Min[1]))
        StartCode |= 4;
    else if (ray_start_point.k[1] > Max[1])
        StartCode |= 8;
    if (!(ray_start_point.k[2] >= Min[2]))
        StartCode |= 0x10;
    else if (ray_start_point.k[2] > Max[2])
        StartCode |= 0x20;
    if (static_cast<int16_t>(StartCode) == 0)
        return 1;

    int32_t EndCode = 0;
    if (!(ray_end_point.k[0] >= Min[0]))
        EndCode = 1;
    else if (ray_end_point.k[0] > Max[0])
        EndCode = 2;
    if (!(ray_end_point.k[1] >= Min[1]))
        EndCode |= 4;
    else if (ray_end_point.k[1] > Max[1])
        EndCode |= 8;
    if (!(ray_end_point.k[2] >= Min[2]))
        EndCode |= 0x10;
    else if (ray_end_point.k[2] > Max[2])
        EndCode |= 0x20;
    if (static_cast<int16_t>(EndCode) == 0)
        return 1;

    if (static_cast<int16_t>(StartCode & EndCode) != 0)
        return 0;

    const IVP_U_Point& S = ray_start_point;
    const IVP_U_Point& E = ray_end_point;

    // :06349d62 -- start below Min.z: face z = Min.z, x/y within [Min, (Max.x, Max.y, Min.z)].
    if ((StartCode & 0x10) != 0 && (EndCode & 0x10) == 0)
    {
        const float PlaneDist = (float)((double)(Min[2]) - S.k[2]);
        const float Scale = (float)((double)(E.k[2]) - S.k[2]);
        const float Corner[3] = { Max[0], Max[1], Min[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Min, Corner, 0, 1) != 0)
            return 1;
    }
    // :06349dc7 -- start beyond Max.x: face x = Max.x, y/z within [(Max.x, Min.y, Min.z), Max].
    if ((StartCode & 0x2) != 0 && (EndCode & 0x2) == 0)
    {
        const float PlaneDist = (float)((double)(S.k[0]) - Max[0]);
        const float Scale = (float)((double)(S.k[0]) - E.k[0]);
        const float Corner[3] = { Max[0], Min[1], Min[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Corner, Max, 1, 2) != 0)
            return 1;
    }
    // :06349e2c -- start beyond Max.z: face z = Max.z, x/y within [(Min.x, Min.y, Max.z), Max].
    if ((StartCode & 0x20) != 0 && (EndCode & 0x20) == 0)
    {
        const float PlaneDist = (float)((double)(S.k[2]) - Max[2]);
        const float Scale = (float)((double)(S.k[2]) - E.k[2]);
        const float Corner[3] = { Min[0], Min[1], Max[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Corner, Max, 0, 1) != 0)
            return 1;
    }
    // :06349e90 -- start below Min.x: face x = Min.x, y/z within [Min, (Min.x, Max.y, Max.z)].
    if ((StartCode & 0x1) != 0 && (EndCode & 0x1) == 0)
    {
        const float PlaneDist = (float)((double)(Min[0]) - S.k[0]);
        const float Scale = (float)((double)(E.k[0]) - S.k[0]);
        const float Corner[3] = { Min[0], Max[1], Max[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Min, Corner, 1, 2) != 0)
            return 1;
    }
    // :06349ef3 -- start below Min.y: face y = Min.y, x/z within [Min, (Max.x, Min.y, Max.z)].
    if ((StartCode & 0x4) != 0 && (EndCode & 0x4) == 0)
    {
        const float PlaneDist = (float)((double)(Min[1]) - S.k[1]);
        const float Scale = (float)((double)(E.k[1]) - S.k[1]);
        const float Corner[3] = { Max[0], Min[1], Max[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Min, Corner, 0, 2) != 0)
            return 1;
    }
    // :06349f58 -- start beyond Max.y: face y = Max.y, x/z within [(Min.x, Max.y, Min.z), Max].
    if ((StartCode & 0x8) != 0 && (EndCode & 0x8) == 0)
    {
        const float PlaneDist = (float)((double)(S.k[1]) - Max[1]);
        const float Scale = (float)((double)(S.k[1]) - E.k[1]);
        const float Corner[3] = { Min[0], Max[1], Min[2] };
        if (check_ray_against_box_face(PlaneDist, Scale, Corner, Max, 0, 2) != 0)
            return 1;
    }
    return 0;
}

// FUN_0634a600. Ray against a sphere of radius Ball+0x94 around the cached world position
// (cache slot +0x4c).
void IVP_Ray_Solver::check_ray_against_ball(IVP_Real_Object* Ball)
{
    IVP_NarrowPhaseCacheSlot* Cache = Ball->get_cache_object_no_lock(); // :0634a609-0634a642
    const float Cx = Cache->WorldTransform.vv.k[0];
    const float Cy = Cache->WorldTransform.vv.k[1];
    const float Cz = Cache->WorldTransform.vv.k[2];

    const double Radius = Ball->field_94;
    const double RadiusSq = Radius * Radius;

    // :0634a66c-0634a69e: q = (az*az + ay*ay) + ax*ax; FCOM r^2; TEST AH,0x1; JNZ -> the ray
    // starts inside the ball (or unordered): no hit.
    const double Ax = (double)(ray_start_point.k[0]) - Cx;
    const double Ay = (double)(ray_start_point.k[1]) - Cy;
    const double Az = (double)(ray_start_point.k[2]) - Cz;
    const double QuadDist = (Az * Az + Ay * Ay) + Ax * Ax;
    if (!(QuadDist >= RadiusSq))
        return;

    // :0634a6a4-0634a6e9: t^2 + 2(a.d) t + (|a|^2 - r^2) = 0.
    IVP_U_Point Coefficients;
    Coefficients.k[0] = 1.0f;
    Coefficients.k[1] = (float)(
        ((Ay * ray_normized_direction.k[1] + Az * ray_normized_direction.k[2]) + Ax * ray_normized_direction.k[0]) * 2.0);
    Coefficients.k[2] = (float)(QuadDist - RadiusSq);

    IVP_U_Point Solution;
    Solution.set_quadratic_roots(&Coefficients); // FUN_06347fa0
    if (!(Solution.k[0] >= 0.0f))
        return;

    // :0634a705-0634a735: the nearer root unless it is behind the start.
    float Dist = Solution.k[1];
    if (!(Dist >= 0.0f))
    {
        Dist = Solution.k[2];
        if (!(Dist >= 0.0f))
            return;
    }

    // :0634a73b-0634a781: hit point (x stays extended), normal = hit - centre.
    const double HitX = (double)(Dist) * ray_normized_direction.k[0] + ray_start_point.k[0];
    const float HitY = (float)((double)(Dist) * ray_normized_direction.k[1] + ray_start_point.k[1]);
    const float HitZ = (float)((double)(Dist) * ray_normized_direction.k[2] + ray_start_point.k[2]);

    IVP_U_Point Normal;
    Normal.k[0] = (float)(HitX - Cx);
    Normal.k[1] = (float)((double)(HitY) - Cy);
    Normal.k[2] = (float)((double)(HitZ) - Cz);
    if (Normal.normize() != 1) // FUN_06347ef0; CMP EAX,1; JZ
    {
        Normal.k[0] = 1.0f;
        Normal.k[1] = 0.0f;
        Normal.k[2] = 0.0f;
    }

    add_hit_object(Ball, nullptr, nullptr, Dist, &Normal); // :0634a7a7-0634a7ba, CALL [EDX]
}

// FUN_0634a7d0. Filter by ray_flags (only when Object is non-null), then dispatch on
// IVP_Object::object_type. Retail reads object_type even for a null Object (:0634a7fe).
void IVP_Ray_Solver::check_ray_against_object(IVP_Real_Object* Object)
{
    if (Object != nullptr)
    {
        const int32_t Flags = ray_flags;
        if ((Flags & 0x1) != 0 && Object->first_child != nullptr)
            return;
        if ((Flags & 0x2) != 0 && (Object->flags & 0x10) == 0)
            return;
        if ((Flags & 0x4) != 0 && (Object->flags & 0x10) != 0)
            return;
    }

    switch (Object->object_type)
    {
    case 2: // polygon: surface manager slot 6 (CALL [ESI+0x18])
        Object->surface_manager->insert_all_ledges_hitting_ray(this, Object);
        break;
    case 3: // ball
        check_ray_against_ball(Object);
        break;
    default:
        break;
    }
}

// FUN_0634a830. The node cell is decoded by FUN_0635d360 into an origin and one edge
// length; box = [origin, origin + size] (floats). On a box hit every element of the cell is
// tested (last to first) against the ray's bounding sphere and then the ray line, and the
// children are walked last to first.
void IVP_Ray_Solver::check_ray_against_ov_node(IVP_OV_Node* Node, IVP_OV_Tree_Manager* Manager)
{
    float Min[3];
    float Size;
    Manager->node_cell_bounds(Node, Min, &Size);
    const float Max[3] = {
        (float)((double)(Min[0]) + Size),
        (float)((double)(Min[1]) + Size),
        (float)((double)(Min[2]) + Size),
    };
    if (check_ray_against_box(Min, Max) == 0)
        return;

    for (int32_t Index = static_cast<int32_t>(Node->Elements.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_OV_Element* Element = Node->Elements.elems[Index];
        const IVP_Core* Core = Element->real_object->core;
        const float Radius = Core->UpperLimitRadius;
        const float Reach = (float)((double)(ray_length) * 0.5 + Radius);

        IVP_U_Point Delta;
        Delta.k[0] = (float)((double)(Core->MWorldFCore.vv.k[0]) - ray_center_point.k[0]);
        Delta.k[1] = (float)((double)(Core->MWorldFCore.vv.k[1]) - ray_center_point.k[1]);
        Delta.k[2] = (float)((double)(Core->MWorldFCore.vv.k[2]) - ray_center_point.k[2]);

        // :0634a8f9-0634a924: (dy*dy + dz*dz) + dx*dx; FCOMPP(reach^2, q); TEST AH,0x41; JNZ skip.
        const double QuadDist = ((double)(Delta.k[1]) * Delta.k[1] + (double)(Delta.k[2]) * Delta.k[2]) +
                                (double)(Delta.k[0]) * Delta.k[0];
        if (!((double)(Reach) * Reach > QuadDist))
            continue;

        // :0634a926-0634a950: distance of the centre from the ray line.
        IVP_U_Point Cross;
        IVP_RayCrossProduct(&ray_normized_direction, &Delta, &Cross);
        if (!((double)(Radius) * Radius > IVP_RayQuadLength(&Cross)))
            continue;

        check_ray_against_object(Element->real_object);
    }

    for (int32_t Index = static_cast<int32_t>(Node->Children.n_elems) - 1; Index >= 0; --Index)
        check_ray_against_ov_node(Node->Children.elems[Index], Manager);
}

// FUN_0634a990. Environment+0x14 is the OV tree manager; +0x178 its current root cell.
void IVP_Ray_Solver::check_ray_against_all_objects_in_sim(const IVP_Environment* Environment)
{
    IVP_OV_Tree_Manager* Manager = Environment->ov_tree_manager;
    if (Manager != nullptr)
        check_ray_against_ov_node(Manager->CurrentRoot, Manager);
}

// ============================================================================
// IVP_Ray_Solver_Os
// ============================================================================

// FUN_0634a500. Start goes through the object's cached world transform inverse
// (FUN_0634d860), the direction through its rotation inverse (FUN_0634d970);
// end = start + L * dir, centre = start + (L * 0.5) * dir (float(h * d + s) each).
IVP_Ray_Solver_Os::IVP_Ray_Solver_Os(IVP_Ray_Solver* RaySolver, IVP_Real_Object* Object)
{
    object = Object;         // +0x48
    ray_solver = RaySolver;  // +0x44

    IVP_NarrowPhaseCacheSlot* Cache = Object->get_cache_object_no_lock(); // :0634a51a-0634a551
    IVP_TransformWorldToSlotSpace(Cache, &RaySolver->ray_start_point, &ray_start_point);
    IVP_RotateWorldToSlotSpace(Cache, &RaySolver->ray_normized_direction, &ray_normized_direction);

    ray_length = RaySolver->ray_length; // :0634a57f-0634a582
    const double Length = ray_length;
    ray_end_point.k[0] = (float)(Length * ray_normized_direction.k[0] + ray_start_point.k[0]);
    ray_end_point.k[1] = (float)(Length * ray_normized_direction.k[1] + ray_start_point.k[1]);
    ray_end_point.k[2] = (float)(Length * ray_normized_direction.k[2] + ray_start_point.k[2]);

    const double Half = Length * 0.5; // DAT_063a5460
    ray_center_point.k[0] = (float)(Half * ray_normized_direction.k[0] + ray_start_point.k[0]);
    ray_center_point.k[1] = (float)(Half * ray_normized_direction.k[1] + ray_start_point.k[1]);
    ray_center_point.k[2] = (float)(Half * ray_normized_direction.k[2] + ray_start_point.k[2]);
}

// FUN_06349fd0 (vtable 063a54b0 slot 0).
int IVP_Ray_Solver_Os::check_ray_against_compact_ledge_os(const IVP_Compact_Ledge* Ledge)
{
    const int32_t NTriangles = Ledge->GetNTriangles(); // MOVSX [ESI+0xc]
    IVP_U_Point Normal;
    IVP_U_Point HitPoint;
    IVP_Unscaled_QR_Result QR;

    const double Sx = ray_start_point.k[0], Sy = ray_start_point.k[1], Sz = ray_start_point.k[2];
    const double Ex = ray_end_point.k[0], Ey = ray_end_point.k[1], Ez = ray_end_point.k[2];

    if (NTriangles == 2)
    {
        // :06349fee-0634a147 -- a two-sided single triangle: one plane test, either side.
        const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
        const IVP_Compact_Edge* Edge = Triangle->GetFirstEdge();
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Edge, Ledge, &Normal);
        const IVP_U_Float_Point* Point = Ledge->GetPoint(Edge);
        const double PlaneDist = IVP_RayDotZYX(&Normal, Point->X, Point->Y, Point->Z);
        const float StartDist = (float)(IVP_RayDotZYX(&Normal, Sx, Sy, Sz) - PlaneDist);
        const double EndDist = IVP_RayDotZYX(&Normal, Ex, Ey, Ez) - PlaneDist;

        // FCOMP 0; TEST AH,0x1; JNZ hit -> only a sign change (or unordered) goes on.
        if (EndDist * StartDist >= 0.0)
            return 0;

        const float HitFraction = (float)(StartDist / (StartDist - EndDist));
        HitPoint.set_interpolate(&ray_start_point, &ray_end_point, HitFraction); // FUN_06347e00
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Ledge, Edge, &HitPoint, &QR);

        // :0634a0bc-0634a0d4: any sign bit set (including -0.0) -> outside the triangle.
        if (((IVP_RayFloatBits(QR.Checks[1]) | IVP_RayFloatBits(QR.Checks[2]) | IVP_RayFloatBits(QR.Checks[0])) &
             0x80000000u) != 0)
            return 0;

        if (!(StartDist >= 0.0f)) // :0634a0d6-0634a115: hit from the back, flip the normal
        {
            Normal.k[0] = (float)((double)(Normal.k[0]) * -1.0); // DAT_063a5498
            Normal.k[1] = (float)((double)(Normal.k[1]) * -1.0);
            Normal.k[2] = (float)((double)(Normal.k[2]) * -1.0);
        }
        Normal.normize(); // FUN_06347ef0

        const float HitDist = (float)((double)(HitFraction) * ray_length);
        ray_solver->add_hit_object(object, Ledge, Triangle, HitDist, &Normal);
        return 1;
    }

    // :0634a14e-0634a20b -- find the first triangle that faces the ray (n.dir <= -1e-6) and
    // has the start point strictly in front of it.
    const double Dx = ray_normized_direction.k[0];
    const double Dy = ray_normized_direction.k[1];
    const double Dz = ray_normized_direction.k[2];
    const double FacingLimit = (double)(-1e-6f); // DAT_063a54a8

    if (NTriangles <= 0)
        return 0;

    const IVP_Compact_Edge* Edge = Ledge->GetFirstEdge(); // ledge + 0x14
    double PlaneDist = 0.0;
    bool bFoundEntry = false;
    for (int32_t Index = 0; Index < NTriangles; ++Index)
    {
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Edge, Ledge, &Normal);
        const double Facing = IVP_RayDotZYX(&Normal, Dx, Dy, Dz);
        if (!(Facing > FacingLimit)) // TEST AH,0x41; JZ skip -> only n.dir > -1e-6 is skipped
        {
            const IVP_U_Float_Point* Point = Ledge->GetPoint(Edge);
            PlaneDist = IVP_RayDotZYX(&Normal, Point->X, Point->Y, Point->Z);
            if (IVP_RayDotZYX(&Normal, Sx, Sy, Sz) - PlaneDist > 0.0)
            {
                bFoundEntry = true;
                break;
            }
        }
        Edge += 4; // ADD EBX,0x10: first edge of the next triangle
    }
    if (!bFoundEntry)
        return 0;

    // :0634a217-0634a24a: the end must not be in front of that plane too.
    if (IVP_RayDotZYX(&Normal, Ex, Ey, Ez) - PlaneDist > 0.0)
        return 0;
    if (Edge == nullptr)
        return 0;

    // :0634a250-0634a393 -- walk across the hull: intersect the current triangle's plane and
    // cross the first edge whose opposite vertex weight is not positive; at most NTriangles
    // steps.
    for (int32_t Remaining = NTriangles - 1; Remaining >= 0; --Remaining)
    {
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Edge, Ledge, &Normal);
        if (IVP_RayDotZYX(&Normal, Dx, Dy, Dz) > FacingLimit) // JZ return
            return 0;

        const IVP_U_Float_Point* Point = Ledge->GetPoint(Edge);
        PlaneDist = IVP_RayDotZYX(&Normal, Point->X, Point->Y, Point->Z);
        const float StartDist = (float)(IVP_RayDotZYX(&Normal, Sx, Sy, Sz) - PlaneDist);
        const double EndDist = IVP_RayDotZYX(&Normal, Ex, Ey, Ez) - PlaneDist;
        if (EndDist * StartDist >= 0.0) // TEST AH,0x1; JZ return
            return 0;

        const float HitFraction = (float)(StartDist / (StartDist - EndDist));
        HitPoint.set_interpolate(&ray_start_point, &ray_end_point, HitFraction);
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Ledge, Edge, &HitPoint, &QR);

        const IVP_Compact_Edge* CrossEdge = Edge;
        int32_t Check = 0;
        for (; Check < 3; ++Check)
        {
            if (!(QR.Checks[Check] > 0.0f)) // TEST AH,0x41; JNZ cross
                break;
            CrossEdge = CrossEdge->GetNextEdge(); // DAT_063ee4e0
        }

        if (Check == 3)
        {
            // :0634a3a5-0634a3d1.
            const float HitDist = (float)((double)(HitFraction) * ray_length);
            Normal.normize();
            ray_solver->add_hit_object(object, Ledge, Edge->GetCompactTriangle(), HitDist, &Normal);
            return 1;
        }

        Edge = CrossEdge->GetOppositeEdge(); // (data << 1) >> 17 edges away
    }
    return 0;
}

// FUN_0634a3f0. Sphere test of the node (radius + half the ray length around the ray
// centre), then -- when the ray centre is outside the node sphere -- the distance of the
// node centre from the ray line (|dir x d|^2 may exceed r^2 by at most 0.01). Terminal
// nodes go to the virtual ledge test, inner nodes recurse left and loop right.
void IVP_Ray_Solver_Os::check_ray_against_ledge_tree_node(const IVP_Compact_Ledgetree_Node* Node)
{
    for (;;)
    {
        const float Radius = Node->radius;
        const float Reach = (float)((double)(ray_length) * 0.5 + Radius);

        IVP_U_Point Delta;
        Delta.k[0] = (float)((double)(Node->center[0]) - ray_center_point.k[0]);
        Delta.k[1] = (float)((double)(Node->center[1]) - ray_center_point.k[1]);
        Delta.k[2] = (float)((double)(Node->center[2]) - ray_center_point.k[2]);
        const double QuadDist = IVP_RayQuadLength(&Delta); // FUN_062c4780

        // FCOMPP(q, reach^2); TEST AH,0x1; JZ return -> q >= reach^2 misses.
        if (QuadDist >= (double)(Reach) * Reach)
            return;

        const float RadiusSq = (float)((double)(Radius) * Radius);
        if (QuadDist >= RadiusSq) // TEST AH,0x1; JNZ skips the line test when q < r^2 (or unordered)
        {
            IVP_U_Point Cross;
            IVP_RayCrossProduct(&ray_normized_direction, &Delta, &Cross); // FUN_0628bed0
            // FSUBR r^2; FCOMP -0.01f; TEST AH,0x1; JNZ return.
            if (!((double)(RadiusSq) - IVP_RayQuadLength(&Cross) >= (double)(-0.01f)))
                return;
        }

        if (Node->offset_right_node == 0)
        {
            check_ray_against_compact_ledge_os(Node->get_compact_ledge()); // CALL [EAX]
            return;
        }

        check_ray_against_ledge_tree_node(Node->left_son());
        Node = Node->right_son();
    }
}

// FUN_0634a4e0.
void IVP_Ray_Solver_Os::check_ray_against_compact_surface(const IVP_Compact_Surface* Surface)
{
    check_ray_against_ledge_tree_node(Surface->get_compact_ledge_tree_root());
}

// LAB_06349b40 (.text:06349b40-06349b94, RET 0x14), IVP_Ray_Solver_Min_Hash::
// add_hit_object (cmn-ivp3d 2026-10-08). The slot is hits[counter] (min_hash
// +0x10 = this+0x5c); a full table (counter >= 0x100, signed `JGE`) drops the
// hit. The counter is raised by min_hash.add, not here.
void IVP_Ray_Solver_Min_Hash::add_hit_object(IVP_Real_Object* Object, const IVP_Compact_Ledge* Ledge,
                                             const IVP_Compact_Triangle* Triangle, float HitDist,
                                             IVP_U_Point* HitSurfaceNormal)
{
    const int32_t Count = min_hash.counter; // :06349b40
    if (Count >= 0x100)
    {
        return;
    }
    IVP_Ray_Hit* const Hit = &hits[Count];                         // :06349b4e-:06349b54
    Hit->hit_real_object = Object;                                 // :06349b56
    Hit->hit_compact_ledge = Ledge;                                // :06349b5d
    Hit->hit_compact_triangle = Triangle;                          // :06349b64
    Hit->hit_surface_direction_os.z = HitSurfaceNormal->z;         // :06349b76
    Hit->hit_surface_direction_os.x = HitSurfaceNormal->x;         // :06349b7d
    Hit->hit_surface_direction_os.y = HitSurfaceNormal->y;         // :06349b81
    Hit->hit_distance = HitDist;                                   // :06349b8b
    min_hash.add(Hit, HitDist);                                    // :06349b8e FUN_0634d3e0
}
