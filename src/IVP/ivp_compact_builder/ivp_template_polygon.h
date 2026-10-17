// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_compact_ledge.h"

// ============================================================================
// IVP_Template_Polygon -- the IVP SDK's polygonal description of a convex
// object (points, lines, surfaces), the input of IVP_SurfaceBuilder_Polygon_
// Convex. Built only by the pointsoup builder (0636b830) in Common.dll.
// (2026-10-08, claude-code:cmn-ivp4b)
// ============================================================================

class IVP_Template_Polygon;

// One line (edge) of the template: two point indices (4 bytes, read as a
// ushort pair by 0636b7c0 and 06379100).
struct IVP_Template_Line
{
    uint16_t p[2];
};
static_assert(sizeof(IVP_Template_Line) == 4, "IVP_Template_Line is 4 bytes");

// 0x20 bytes, allocated as an array by the eh vector constructor iterator
// (0636b98e) with 0636d9c0 / 0636da20 as element ctor / dtor.
class IVP_Template_Surface
{
public:
    float normal[4];                 // +0x00 plane normal (x, y, z; +0xc not written by the builder)
    IVP_Template_Polygon* templ;     // +0x10 owning template
    int32_t n_lines;                 // +0x14
    uint16_t* lines;                 // +0x18 line indices, calloc(n_lines, 2) (FUN_06343f80)
    char* revert_line;               // +0x1c per line: 1 = walk the line forwards, operator new(n_lines)

    // 0636d9c0: zero 8 dwords.
    IVP_Template_Surface();
    // 0636d9e0 (reached through the thunk 0636da20).
    ~IVP_Template_Surface();

    // 0636da10: (this - templ->surfaces) >> 5.
    int32_t get_surface_index() const;
};
static_assert(sizeof(IVP_Template_Surface) == 0x20, "IVP_Template_Surface is 0x20 bytes");

// 0x18 bytes (operator new at 0636b84c).
class IVP_Template_Polygon
{
public:
    int32_t n_points;                 // +0x00
    IVP_U_Float_Point* points;        // +0x04 operator new(n_points * 0x10)
    int32_t n_lines;                  // +0x08
    IVP_Template_Line* lines;         // +0x0c operator new(n_lines * 4)
    int32_t n_surfaces;               // +0x10
    IVP_Template_Surface* surfaces;   // +0x14 new IVP_Template_Surface[n_surfaces]

    // 0636d940.
    IVP_Template_Polygon();
    // 0636d960.
    ~IVP_Template_Polygon();
};
static_assert(sizeof(IVP_Template_Polygon) == 0x18, "IVP_Template_Polygon is 0x18 bytes");
