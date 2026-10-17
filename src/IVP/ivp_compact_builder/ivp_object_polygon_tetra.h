// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_math.h"

// ============================================================================
// IVP_Object_Polygon_Tetra and its helpers -- the IVP SDK's triangulated
// working copy of an IVP_Template_Polygon, built by
// IVP_SurfaceBuilder_Polygon_Convex and consumed by
// IVP_Compact_Ledge_Generator (Common.dll .text:06378a10-0637a2d9).
// (2026-10-08, claude-code:cmn-ivp4b)
//
// Every field below is placed at the offset the listing reads or writes; the
// ones no retail instruction in this chain reads keep a Field_0xNN name.
// ============================================================================

class IVP_Hash;
class IVP_Object_Polygon_Tetra;
class IVP_Template_Polygon;
class IVP_Template_Surface;
class IVP_Triangle;
class P_Sur_2D_Line;

// 0x18 bytes: a 16-byte point plus the owning tetra and the compact point
// index the ledge generator assigns (0637a405).
class IVP_Poly_Point : public IVP_U_Point
{
public:
    IVP_Object_Polygon_Tetra* l_tetras; // +0x10
    int32_t compact_index;              // +0x14
};
static_assert(sizeof(IVP_Poly_Point) == 0x18, "IVP_Poly_Point is 0x18 bytes");

// 0x24 bytes, three of them inline in every IVP_Triangle.
class IVP_Tri_Edge
{
public:
    IVP_Poly_Point* start_point; // +0x00
    IVP_Triangle* triangle;      // +0x04
    IVP_Tri_Edge* next;          // +0x08
    IVP_Tri_Edge* prev;          // +0x0c
    IVP_Tri_Edge* behind;        // +0x10 the matching edge of the triangle behind (other side)
    IVP_Tri_Edge* opposite;      // +0x14 the edge across this one
    uint32_t Field_0x18;         // +0x18 zeroed by the triangle ctor, not read by this chain (TODO name)
    int32_t start_point_index;   // +0x1c start_point->compact_index copied by 0637a212 (only writer and no reader in this chain)
    uint32_t Field_0x20;         // +0x20 zeroed by the triangle ctor, not read by this chain (TODO name)

    // 06378a40: the edge of triangle->other_side whose next starts at this
    // edge's start point, or null.
    IVP_Tri_Edge* other_side();
};
static_assert(sizeof(IVP_Tri_Edge) == 0x24, "IVP_Tri_Edge is 0x24 bytes");

// 0xc bytes, calloc'd as an array by the tetra ctor (06379a38).
class IVP_Poly_Surface
{
public:
    uint32_t Field_0x00;              // +0x00 never written (calloc zero) (TODO name)
    IVP_Object_Polygon_Tetra* tetra;  // +0x04
    uint32_t Field_0x08;              // +0x08 never written (calloc zero) (TODO name)

    // 06379b00 (thiscall, RET 8): writes only tetra; the template surface
    // argument is not read.
    void set(IVP_Template_Surface* TemplSurface, IVP_Object_Polygon_Tetra* Tetra);
};
static_assert(sizeof(IVP_Poly_Surface) == 0xc, "IVP_Poly_Surface is 0xc bytes");

// 0x9c bytes, vtable .rdata:063a5998 (one slot: the scalar deleting
// destructor 0637a110; the destructor 0637a130 only restores the vtable).
class IVP_Triangle
{
public:
    // 0637a0e0.
    IVP_Triangle();
    // 0637a130 / slot 0 = 0637a110.
    virtual ~IVP_Triangle();

    IVP_Triangle* next;            // +0x04 tetra triangle list
    IVP_Triangle* prev;            // +0x08
    IVP_Triangle* other_side;      // +0x0c the reversed copy (back side)
    IVP_Triangle* pierce_partner;  // +0x10 most anti-parallel front triangle (0637a2c9)
    uint8_t Field_0x14;            // +0x14 set to 1 for every triangle of make_triangles, never read here (TODO name)
    uint8_t is_back_side;          // +0x15 1 on the reversed copies
    uint8_t Pad_0x16[2];
    IVP_Poly_Surface* ivp_surface; // +0x18
    IVP_Tri_Edge three_edges[3];   // +0x1c
    IVP_U_Point tmp_hesse;         // +0x88 plane of the triangle (06378a10)
    int32_t index;                 // +0x98 position in the generator's triangle vector (0637a3d6)

    // 06378a10: hesse = plane(e0.start, e0.prev.start, e0.next.start), normized.
    void calc_hesse();
};
static_assert(offsetof(IVP_Triangle, three_edges) == 0x1c, "IVP_Triangle edges at +0x1c");
static_assert(offsetof(IVP_Triangle, tmp_hesse) == 0x88, "IVP_Triangle hesse at +0x88");
static_assert(sizeof(IVP_Triangle) == 0x9c, "IVP_Triangle is 0x9c bytes");

// 0x1c bytes: a 2D point of one template surface (x, y in the projection
// plane, z always 0).
class P_Sur_2D_Point : public IVP_U_Point
{
public:
    // 06378aa0 (thiscall, RET 4): zero 7 dwords, point_num = PointNum.
    explicit P_Sur_2D_Point(int32_t PointNum);

    int32_t point_num;     // +0x10 index into the tetra points
    int32_t chain_mark;    // +0x14 set to 1 by mark_point_chain (063793d0), which also stops on it
    P_Sur_2D_Line* line;   // +0x18 the line that starts here (06379378)
};
static_assert(sizeof(P_Sur_2D_Point) == 0x1c, "P_Sur_2D_Point is 0x1c bytes");

// 0x18 bytes: a directed 2D line of the surface outline.
class P_Sur_2D_Line
{
public:
    // 06378ac0 (thiscall, RET 8).
    P_Sur_2D_Line(P_Sur_2D_Point* StartPoint, P_Sur_2D_Point* EndPoint);

    P_Sur_2D_Line* next;          // +0x00
    P_Sur_2D_Line* prev;          // +0x04
    P_Sur_2D_Point* start_point;  // +0x08
    P_Sur_2D_Point* end_point;    // +0x0c
    float delta_x;                // +0x10 end - start
    float delta_y;                // +0x14

    // 06378b00: 0 when the point is not strictly left of the line, else 1
    // (2 is unreachable, see the definition).
    int32_t point_lies_to_the_left(const P_Sur_2D_Point* Point) const;
    // 06378b40: the line joins A and B (either direction); B null tests A only.
    int32_t has_points(const P_Sur_2D_Point* A, const P_Sur_2D_Point* B) const;
    // 06378ba0: signed distance of the point (squared distance to start when
    // the line is degenerate).
    double dist_to_point(const P_Sur_2D_Point* Point) const;
    // 06378c10: unnormalized signed distance.
    double hesse_dist_to_point(const P_Sur_2D_Point* Point) const;
    // 06378c30: the point lies strictly inside the line's extent (dominant axis).
    int32_t point_lies_in_interval(const P_Sur_2D_Point* Point) const;
    // 06378d10: two collinear lines overlap.
    int32_t overlaps_with_line(const P_Sur_2D_Line* Other) const;
    // 06378ea0: the lines cross (or overlap when parallel and collinear).
    int32_t is_crossing_line(const P_Sur_2D_Line* Other) const;
};
static_assert(sizeof(P_Sur_2D_Line) == 0x18, "P_Sur_2D_Line is 0x18 bytes");

// 0x14 bytes: one triangle of the triangulated surface, as point numbers.
class P_Sur_2D_Triangle
{
public:
    // 06378a70 (thiscall, RET 0xc).
    P_Sur_2D_Triangle(int32_t Point0, int32_t Point1, int32_t Point2);

    P_Sur_2D_Triangle* next;     // +0x00
    P_Sur_2D_Triangle* prev;     // +0x04
    int32_t point_nums[3];       // +0x08
};
static_assert(sizeof(P_Sur_2D_Triangle) == 0x14, "P_Sur_2D_Triangle is 0x14 bytes");

// 0x20 bytes: the 2D projection of one template surface.
class P_Sur_2D
{
public:
    // 06378fd0 (thiscall, RET 8).
    P_Sur_2D(IVP_Object_Polygon_Tetra* Tetra, IVP_Template_Surface* OrigSurface);
    // 06379000.
    ~P_Sur_2D();

    // 06379100: builds the outline lines and points; returns an error text or null.
    const char* calc_line_representation();
    // 06379460: ear clipping of the outline into triangles; returns an error text or null.
    const char* calc_triangle_representation();

    IVP_Object_Polygon_Tetra* tetra;        // +0x00
    IVP_Template_Surface* orig_surface;     // +0x04
    P_Sur_2D_Line* lines;                   // +0x08 list head
    int32_t n_lines;                        // +0x0c
    P_Sur_2D_Triangle* triangles;           // +0x10 list head
    int32_t n_triangles;                    // +0x14
    P_Sur_2D_Line** line_array;             // +0x18 calloc(orig n_lines, 4), the line of each template line
    P_Sur_2D_Point** point_array;           // +0x1c calloc(orig n_lines, 4), every point created

private:
    // 063793d0 (cdecl): sets chain_mark along point -> line -> end point
    // until a marked point; returns the number of points marked.
    static int32_t mark_point_chain(P_Sur_2D_Point* Point);
    // 06379400 (cdecl): |P2 - P0|^2 + |P2 - P1|^2.
    static double sum_quad_dists(const IVP_U_Point* P0, const IVP_U_Point* P1, const IVP_U_Point* P2);
};
static_assert(sizeof(P_Sur_2D) == 0x20, "P_Sur_2D is 0x20 bytes");

// 0x50 bytes (operator new at 0636da86).
class IVP_Object_Polygon_Tetra
{
public:
    // 06379a10 (thiscall, RET 4).
    explicit IVP_Object_Polygon_Tetra(IVP_Template_Polygon* Templ);
    // 06379b10.
    ~IVP_Object_Polygon_Tetra();

    // 06379b50: one front triangle per 2D triangle of every surface, then the
    // back side copies; returns an error text or null.
    const char* make_triangles();
    // 06379ff0.
    void free_triangles();
    // 0637a240: pairs every front triangle with the most anti-parallel one.
    void pair_pierce_triangles();

    // 0637a140 (cdecl): a triangle and its reversed copy over three points.
    static IVP_Triangle* make_double_triangle(IVP_Poly_Point* P1, IVP_Poly_Point* P2, IVP_Poly_Point* P3);

    IVP_Template_Polygon* template_polygon; // +0x00 cleared by the ctor's last store, set by the caller
    uint32_t Field_0x04[5];                 // +0x04..+0x17 zeroed by the ctor, not read here (TODO name)
    IVP_Hash* edge_hash;                    // +0x18 live only around make_triangles (0636df1b)
    uint32_t Field_0x1c[5];                 // +0x1c..+0x2f zeroed by the ctor, not read here (TODO name)
    IVP_Triangle* triangles;                // +0x30 list head (next +0x04, prev +0x08)
    int32_t n_triangles;                    // +0x34
    IVP_Poly_Point* points;                 // +0x38 calloc(n_points, 0x18)
    int32_t n_points;                       // +0x3c
    IVP_Poly_Surface* surfaces;             // +0x40 calloc(n_surfaces, 0xc)
    int32_t n_surfaces;                     // +0x44
    void* Field_0x48;                       // +0x48 chain freed by free_triangles (next at +0x18); no writer in this chain (TODO type)
    uint32_t Field_0x4c;                    // +0x4c zeroed by the ctor (TODO name)

private:
    // 063799b0 (cdecl): links Edge with the edge already stored under the
    // point pair {A, B}, or stores Edge.
    static void link_edge(IVP_Hash* Hash, IVP_Poly_Point* A, IVP_Poly_Point* B, IVP_Tri_Edge* Edge);
    // 0637a060 (cdecl).
    static void init_tri_edges(IVP_Triangle* Triangle);
    // 0637a090 (thiscall, RET 4): registers Edge in edge_hash under its point pair.
    void add_edge_to_hash(IVP_Tri_Edge* Edge);
};
static_assert(offsetof(IVP_Object_Polygon_Tetra, edge_hash) == 0x18, "tetra edge_hash at +0x18");
static_assert(offsetof(IVP_Object_Polygon_Tetra, triangles) == 0x30, "tetra triangles at +0x30");
static_assert(offsetof(IVP_Object_Polygon_Tetra, Field_0x48) == 0x48, "tetra Field_0x48 at +0x48");
static_assert(sizeof(IVP_Object_Polygon_Tetra) == 0x50, "IVP_Object_Polygon_Tetra is 0x50 bytes");
