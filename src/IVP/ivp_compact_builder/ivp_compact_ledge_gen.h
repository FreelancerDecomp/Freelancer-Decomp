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
// IVP_Compact_Ledge_Generator -- turns the triangles of an
// IVP_Object_Polygon_Tetra (or a single triangle pair) into one flat
// IVP_Compact_Ledge (Common.dll .text:0637a2e0-0637a86f).
// (2026-10-08, claude-code:cmn-ivp4b)
//
// Used on the stack by IVP_SurfaceBuilder_Polygon_Convex: ctor, then
// prepare_compact_ledge (returns the byte size), the caller allocates that
// many bytes with FUN_06343fc0(size, 0x10), then generate_compact_ledge
// fills them, then the destructor.
// ============================================================================

class FPolygonPointHash;
class IVP_Hash;
class IVP_Poly_Point;
class IVP_Triangle;

// 0x30 bytes.
class IVP_Compact_Ledge_Generator
{
public:
    // 0637a2e0: every field zero.
    IVP_Compact_Ledge_Generator();
    // 0637a750.
    ~IVP_Compact_Ledge_Generator();

    // 0637a330 (thiscall, RET 4): numbers the points, builds the compact
    // triangles and their opposite links; returns the ledge size in bytes.
    int32_t prepare_compact_ledge(IVP_U_Vector<IVP_Triangle>* Triangles);
    // 0637a6a0 (thiscall, RET 4): writes header, triangles and points into
    // Buffer (prepare_compact_ledge's size, 16-byte aligned).
    void generate_compact_ledge(void* Buffer);

    FPolygonPointHash* point_hash;                   // +0x00 vtable 063a5918
    IVP_U_Vector<IVP_Poly_Point> points;             // +0x04
    IVP_U_Vector<IVP_Compact_Triangle> c_triangles;  // +0x0c
    int32_t n_points;                                // +0x14
    int32_t n_edges;                                 // +0x18 counted by prepare, not read
    int32_t n_triangles;                             // +0x1c
    IVP_Hash* edge_hash;                             // +0x20 edge pointer -> compact edge unit
    uint32_t Field_0x24;                             // +0x24 zeroed by the ctor only (TODO name)
    IVP_Compact_Ledge* compact_ledge;                // +0x28
    IVP_U_Vector<IVP_Triangle>* triangles;           // +0x2c
};
static_assert(sizeof(IVP_Compact_Ledge_Generator) == 0x30, "IVP_Compact_Ledge_Generator is 0x30 bytes");
