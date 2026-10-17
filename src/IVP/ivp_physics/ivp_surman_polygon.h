// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP_SurfaceManager_Polygon -- the surface manager of every polygon (object_type 2) real
// object: it answers the IVP_SurfaceManager queries from one compiled IVP_Compact_Surface.
//
// Retail evidence:
//   * RTTI: vtable .rdata:0639c2dc, COL 063a731c -> TD .data:063eb134
//     ".?AVIVP_SurfaceManager_Polygon@@"; the destructor 0634ad60 re-stamps 0639c2dc and
//     tail-jumps into the base destructor 0635d440.
//   * vtable 0639c2dc, 11 slots (same order as the base table 063a56ac):
//       0 0634ad40 get_single_convex          6 0634ab60 insert_all_ledges_hitting_ray
//       1 0634ac70 get_mass_center            7 062ed730 add_reference_to_ledge (base, RET 4)
//       2 0634acb0 get_radius_and_radius_dev  8 062ed730 remove_reference_to_ledge (base)
//       3 0634ac90 get_rotation_inertia       9 0628b050 scalar deleting dtor -> 0634ad60
//       4 0634ab90 get_all_ledges_within_radius
//       5 0634ac50 get_all_terminal_ledges   10 06325390 get_type = 0 (XOR EAX,EAX; RET)
//   * Layout: 8 bytes, {vptr, +0x04 const IVP_Compact_Surface*}. The constructor is inlined
//     at every site: `new(8); [+4] = surface; [+0] = 0639c2dc` (0628aed9-0628aef0) -- and
//     PhySys::PhySurfaceManager (vtable 0639c258, new(0xc), 0628a14b-0628a165) derives from
//     it, adding an owner pointer at +0x08 and overriding slots 4/5/6/9.
//   * PhySys calls the base implementations of slots 4/5/6 directly (06285f35 -> 0634ab90,
//     06285faa -> 0634ac50, 06285e94/06285ead -> 0634a500 / 0634a4e0).

#include <cstdint>
#include "ivp_surface_manager.h"

class IVP_Compact_Surface;

#pragma pack(push, 4)

class IVP_SurfaceManager_Polygon : public IVP_SurfaceManager
{
public:
    // Inlined in retail (see above): stores the surface, installs the vtable.
    explicit IVP_SurfaceManager_Polygon(const IVP_Compact_Surface* CompactSurface);

    // 0634ad60 (+ the scalar deleting wrapper 0628b050). Only restores the vptr.
    ~IVP_SurfaceManager_Polygon() override;

    // Slot 0, 0634ad40: the root's ledge when the root is terminal, otherwise the root's
    // hull ledge if it has one, otherwise null. Returns const IVP_Compact_Ledge*.
    void* get_single_convex() override;

    // Slot 1, 0634ac70: copies IVP_Compact_Surface::mass_center (3 floats) to OutVec3.
    float* get_mass_center(float* OutVec3) override;

    // Slot 2, 0634acb0: Param1 = const IVP_U_Point* center, Param2 = float* radius,
    // Param3 = float* radius_deviation.
    void get_radius_and_radius_dev_to_given_center(void* Param1, void* Param2, void* Param3) override;

    // Slot 3, 0634ac90: Param1 = float* out (3 floats of rotation_inertia).
    void get_rotation_inertia(void* Param1) override;

    // Slot 4, 0634ab90: Center3 = observer position in object space, Param3 = root ledge
    // (const IVP_Compact_Ledge*, may be null), Param4/Param5 (other object / other reference
    // ledge) are not read by this implementation.
    void get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                      void* Param4, void* Param5,
                                      IVP_SurfaceManager_OutList* OutList) override;

    // Slot 5, 0634ac50: every terminal ledge of the surface (FUN_0635abf0).
    void get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) override;

    // Slot 6, 0634ab60: Param1 = IVP_Ray_Solver*, Param2 = IVP_Real_Object*.
    void insert_all_ledges_hitting_ray(void* Param1, void* Param2) override;

    // Slot 10, 06325390.
    int32_t get_type() override;

    const IVP_Compact_Surface* compact_surface; // +0x04
};
static_assert(sizeof(IVP_SurfaceManager_Polygon) == 0x08, "IVP_SurfaceManager_Polygon size");

#pragma pack(pop)
