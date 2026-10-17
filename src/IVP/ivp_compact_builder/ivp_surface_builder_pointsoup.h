// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_compact_ledge.h"
#include "../ivp_utility/ivp_u_vector.h"

// ============================================================================
// IVP_SurfaceBuilder_Pointsoup -- convex hull of a point cloud as one
// IVP_Compact_Ledge (Common.dll .text:0636b770-0636c9f2).
// (2026-10-08, claude-code:cmn-ivp4b: rewritten from the listing; the former
// direct qhull-to-ledge shortcut is gone.)
//
// Retail path: deduplicated points -> qhull (vendored, src/qhull) -> one
// IVP_SurMan_PS_Plane per facet -> IVP_Template_Polygon ->
// IVP_SurfaceBuilder_Polygon_Convex (IVP_Object_Polygon_Tetra +
// IVP_Compact_Ledge_Generator). Exactly three points take the single
// triangle path instead. The returned ledge is a 16-byte aligned block
// (FUN_06343fc0); the caller releases it with FUN_06343ff0.
// ============================================================================

class IVP_Template_Polygon;

// 0x18 bytes (operator new at 0636be8c): one qhull facet.
class IVP_SurMan_PS_Plane
{
public:
    IVP_SurMan_PS_Plane()
    {
    }

    float hesse[4];                            // +0x00 facet normal (x, y, z written; +0xc never)
    IVP_U_Vector<IVP_U_Float_Point> points;    // +0x10 the facet's points, qhull vertex order

    // 0636bce0: twice the signed area against the normal, summed as floats.
    float get_area_size() const;
    // 0636bdc0: sum of the squared edge lengths (x87, returned unrounded).
    double get_qlen_of_all_edges() const;
};
static_assert(sizeof(IVP_SurMan_PS_Plane) == 0x18, "IVP_SurMan_PS_Plane is 0x18 bytes");

class IVP_SurfaceBuilder_Pointsoup
{
public:
    // 0636c9a0 (cdecl).
    static IVP_Compact_Ledge* convert_pointsoup_to_compact_ledge(IVP_U_Vector<IVP_U_Float_Point>* Points);

    // 0636c7e0 (.text:0636c7e0-0636c7fc), tail-jumped from PhySys::Shutdown:
    // frees the single-triangle template ledge DAT_0658b008 (FUN_06343ff0)
    // when set and clears it.
    static void Cleanup();

private:
    // 0636c260 (cdecl).
    static IVP_Compact_Ledge* convert_pointsoup_to_compact_ledge_internal(IVP_U_Vector<IVP_U_Float_Point>* Points);
    // 0636c800 (cdecl).
    static IVP_Compact_Ledge* single_tri_to_compact_ledge(const IVP_U_Float_Point* P0, const IVP_U_Float_Point* P1,
        const IVP_U_Float_Point* P2);
    // 0636be20 (cdecl).
    static IVP_Compact_Ledge* try_to_build_convex_ledge_from_qhull_result(IVP_U_Vector<IVP_U_Float_Point>* Points,
        int32_t* Retry, char* Excluded, char* Used);
    // 0636b830 (cdecl).
    static IVP_Template_Polygon* planes_to_template(IVP_U_Vector<IVP_U_Float_Point>* Points,
        IVP_U_Vector<IVP_SurMan_PS_Plane>* Planes);
    // 0636b770 (cdecl).
    static int32_t point_index(const IVP_U_Float_Point* Array, int32_t Count, const IVP_U_Float_Point* Point);
    // 0636b7c0 (cdecl).
    static int32_t line_index(const void* Lines, int32_t Count, int32_t A, int32_t B, uint8_t* Revert);
};

// The single-triangle template ledge (retail .data:DAT_0658b008), built
// lazily by single_tri_to_compact_ledge and released by Cleanup().
extern IVP_Compact_Ledge* GPointsoupTriangleTemplate;

// Convex hull of a set of ledges (retail name not in the binary). Object of
// 0xc bytes: +0x0 IVP_U_Vector<IVP_Compact_Ledge> of input ledges, +0x8 the
// resulting hull ledge. Only user: IVP_SurfaceBuilder_Ledge_Soup's
// options[0] step (FUN_0636a1e0).
class FLedgeConvexHullBuilder
{
public:
    // FUN_0636ca00: Ledges = {memsize 0x80, n_elems 0, elems = malloc(0x200)};
    // Result is not initialised by retail (written by BuildHull before use).
    FLedgeConvexHullBuilder();

    // FUN_0636cf70: BuildHull(); if (Result) MarkVirtualFeatures(); return Result.
    IVP_Compact_Ledge* Build();

    IVP_U_Vector<IVP_Compact_Ledge> Ledges; // +0x00
    IVP_Compact_Ledge* Result;              // +0x08

private:
    // FUN_0636ca30: unique points of every input ledge -> FUN_0636c9a0.
    void BuildHull();

    // FUN_0636cc00: flags hull triangles / edges that do not exist in the
    // input ledges (bit 31 of the triangle info / edge dword).
    void MarkVirtualFeatures();
};
