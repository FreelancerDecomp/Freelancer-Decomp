// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP ray casting (Ipion IVP_Ray_Solver family). RTTI in Common.dll proves the class names
// and the hierarchy (TD .data:063eb1d4 IVP_Ray_Hit_Listener, 063eb1f8 IVP_Ray_Solver,
// 063eb218 IVP_Ray_Solver_Min_Hash, IVP_Ray_Solver_Os; CHD 063a7250 lists
// Min_Hash -> IVP_Ray_Solver -> IVP_Ray_Hit_Listener).
//
// vtables:
//   IVP_Ray_Hit_Listener 0639c2d0 { _purecall (add_hit_object), 06289e60 scalar deleting dtor }
//   IVP_Ray_Solver       063a54a0 { _purecall (add_hit_object), 06349b20 scalar deleting dtor }
//   IVP_Ray_Solver_Os    063a54b0 { 06349fd0 check_ray_against_compact_ledge_os } -- one slot,
//                        no virtual destructor (PhySys::RaySolver_MaskedSurface overrides it).
// Both destructors (06289e50 for the listener, ICF-shared by IVP_Ray_Solver's 06349b20) only
// restore the vptr.

#include <cstddef>
#include <cstdint>
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_min_hash.h"

class IVP_Real_Object;
class IVP_Environment;
class IVP_OV_Node;
class IVP_OV_Tree_Manager;
class IVP_Compact_Ledge;
class IVP_Compact_Triangle;
class IVP_Compact_Surface;
class IVP_Compact_Ledgetree_Node;

#pragma pack(push, 4)

// What a caller fills before constructing an IVP_Ray_Solver (read by 06349a60).
struct IVP_Ray_Solver_Template
{
    IVP_U_Point ray_start_point;        // +0x00 (x, y, z read)
    IVP_U_Point ray_normized_direction; // +0x10 (x, y, z read)
    float       ray_length;             // +0x20
    int32_t     ray_flags;              // +0x24, see IVP_Ray_Solver::ray_flags
};
static_assert(sizeof(IVP_Ray_Solver_Template) == 0x28, "IVP_Ray_Solver_Template size");

class IVP_Ray_Hit_Listener
{
public:
    // Slot 0. Called with (object, ledge, triangle, hit distance, hit surface normal in the
    // object's space). The ball path passes ledge = triangle = nullptr (0634a7b3/0634a7b5).
    virtual void add_hit_object(IVP_Real_Object* Object, const IVP_Compact_Ledge* Ledge,
                                const IVP_Compact_Triangle* Triangle, float HitDist,
                                IVP_U_Point* HitSurfaceNormal) = 0;

    // Slot 1: 06289e60 / 06349b20 -> 06289e50, which only restores the vptr.
    virtual ~IVP_Ray_Hit_Listener() {}
};

class IVP_Ray_Solver : public IVP_Ray_Hit_Listener
{
public:
    IVP_U_Point ray_start_point;        // +0x04
    IVP_U_Point ray_end_point;          // +0x14 = start + length * direction
    IVP_U_Point ray_center_point;       // +0x24 = start + (length * 0.5) * direction
    IVP_U_Point ray_normized_direction; // +0x34
    float       ray_length;             // +0x44
    // +0x48. Filter bits tested by check_ray_against_object (0634a7d0):
    //   0x1 skip objects whose IVP_Object+0x1c (phantom controller) is set,
    //   0x2 skip objects whose flags (+0x78) lack bit 0x10,
    //   0x4 skip objects whose flags (+0x78) have bit 0x10.
    int32_t     ray_flags;

    // FUN_06349a60 (thiscall, RET 4).
    explicit IVP_Ray_Solver(const IVP_Ray_Solver_Template* Template);

    // FUN_06349ba0 (thiscall, RET 0x18). For the two axes AxisB then AxisA:
    // V = direction[k] * length * PlaneDist must lie within
    // [(Lo[k] - start[k]) * Scale, (Hi[k] - start[k]) * Scale]; returns 1 when both do.
    int check_ray_against_box_face(float PlaneDist, float Scale, const float* Lo, const float* Hi,
                                   int16_t AxisA, int16_t AxisB);

    // FUN_06349c30 (thiscall, RET 8): segment start..end against the box [Min, Max].
    int check_ray_against_box(const float* Min, const float* Max);

    // FUN_0634a600 (thiscall, RET 4). Retail takes the IVP_Ball as an IVP_Real_Object.
    void check_ray_against_ball(IVP_Real_Object* Ball);

    // FUN_0634a7d0 (thiscall, RET 4).
    void check_ray_against_object(IVP_Real_Object* Object);

    // FUN_0634a830 (thiscall, RET 8): recursive walk of the OV (object volume) tree.
    void check_ray_against_ov_node(IVP_OV_Node* Node, IVP_OV_Tree_Manager* Manager);

    // FUN_0634a990 (thiscall, RET 4).
    void check_ray_against_all_objects_in_sim(const IVP_Environment* Environment);
};
static_assert(sizeof(IVP_Ray_Solver) == 0x4c, "IVP_Ray_Solver size");

// One hit recorded by IVP_Ray_Solver_Min_Hash (0x20 bytes, the stride of
// 06349b40's `(count + 3) << 5` index). Only x/y/z of the normal are written.
struct IVP_Ray_Hit
{
    IVP_U_Point                 hit_surface_direction_os; // +0x00 (x, y, z; +0x0c not written)
    IVP_Real_Object*            hit_real_object;          // +0x10
    const IVP_Compact_Ledge*    hit_compact_ledge;        // +0x14
    const IVP_Compact_Triangle* hit_compact_triangle;     // +0x18
    float                       hit_distance;             // +0x1c
};
static_assert(sizeof(IVP_Ray_Hit) == 0x20, "IVP_Ray_Hit size");

// IVP_Ray_Solver_Min_Hash (cmn-ivp3d 2026-10-08), vtable .rdata:0639c2c4
// {06349b40 add_hit_object, 06289ed0 scalar deleting dtor}; RTTI TD 063eb218.
// Collects up to 0x100 hits, ordered by distance in min_hash. The hit count
// is min_hash.counter (+0x5c), raised by min_hash.add.
// No out-of-line constructor: PhySys builds it inline (06289cc0, 0628d2b9:
// FUN_06349a60, FUN_0634d210(8), vptr 0639c2c4).
class IVP_Ray_Solver_Min_Hash : public IVP_Ray_Solver
{
public:
    IVP_U_Min_Hash min_hash;     // +0x4c (0x14 bytes; counter at +0x5c)
    IVP_Ray_Hit    hits[0x100];  // +0x60

    explicit IVP_Ray_Solver_Min_Hash(const IVP_Ray_Solver_Template* Template)
        : IVP_Ray_Solver(Template)
        , min_hash(8)
    {
    }

    // 06289e80 (in place: vptr 0639c2c4, ~IVP_U_Min_Hash 0634d260, vptr
    // 0639c2d0) / 06289ed0 (scalar deleting, slot 1).
    ~IVP_Ray_Solver_Min_Hash() override {}

    // Slot 0, LAB_06349b40 (RET 0x14). Defined in ivp_ray_solver.cpp.
    void add_hit_object(IVP_Real_Object* Object, const IVP_Compact_Ledge* Ledge,
                        const IVP_Compact_Triangle* Triangle, float HitDist,
                        IVP_U_Point* HitSurfaceNormal) override;
};
static_assert(offsetof(IVP_Ray_Solver_Min_Hash, min_hash) == 0x4c, "IVP_Ray_Solver_Min_Hash+0x4c");
static_assert(offsetof(IVP_Ray_Solver_Min_Hash, hits) == 0x60, "IVP_Ray_Solver_Min_Hash+0x60");
static_assert(sizeof(IVP_Ray_Solver_Min_Hash) == 0x2060, "IVP_Ray_Solver_Min_Hash size");

// The ray carried into one object's space. Built on the stack by
// IVP_SurfaceManager_Polygon::insert_all_ledges_hitting_ray (0634ab60) and by PhySys.
class IVP_Ray_Solver_Os
{
public:
    IVP_U_Point      ray_start_point;        // +0x04 (object space)
    IVP_U_Point      ray_center_point;       // +0x14
    IVP_U_Point      ray_end_point;          // +0x24
    IVP_U_Point      ray_normized_direction; // +0x34
    IVP_Ray_Solver*  ray_solver;             // +0x44: receives add_hit_object
    IVP_Real_Object* object;                 // +0x48
    float            ray_length;             // +0x4c

    // FUN_0634a500 (thiscall, RET 8).
    IVP_Ray_Solver_Os(IVP_Ray_Solver* RaySolver, IVP_Real_Object* Object);

    // Slot 0, FUN_06349fd0 (thiscall, RET 4). Returns 1 when a hit was reported.
    virtual int check_ray_against_compact_ledge_os(const IVP_Compact_Ledge* Ledge);

    // FUN_0634a3f0 (thiscall, RET 4): recursive ledge-tree walk.
    void check_ray_against_ledge_tree_node(const IVP_Compact_Ledgetree_Node* Node);

    // FUN_0634a4e0 (thiscall, RET 4).
    void check_ray_against_compact_surface(const IVP_Compact_Surface* Surface);
};
static_assert(sizeof(IVP_Ray_Solver_Os) == 0x50, "IVP_Ray_Solver_Os size");

#pragma pack(pop)
