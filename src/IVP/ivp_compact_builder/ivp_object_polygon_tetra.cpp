// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_object_polygon_tetra.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ivp_builder_math.h"
#include "ivp_template_polygon.h"
#include "../ivp_utility/ivp_hash.h"
#include "../ivp_utility/ivp_min_hash.h"

// ============================================================================
// Common.dll .text:06378a10-0637a2d9 (2026-10-08, claude-code:cmn-ivp4b).
// Compare idioms used below (FNSTSW AX after FCOM ST0, src):
//   TEST AH,0x41 + JZ  -> taken when ST0 > src (ordered)         == (a > b)
//   TEST AH,0x41 + JNZ -> taken when ST0 <= src or unordered     == !(a > b)
//   TEST AH,0x01 + JZ  -> taken when ST0 >= src (ordered)        == (a >= b)
//   TEST AH,0x01 + JNZ -> taken when ST0 < src or unordered      == !(a >= b)
// ============================================================================

namespace
{
    // .rdata constants, decoded from their raw bytes.
    const float kEpsilon1e10 = IVP_BuilderBitsToFloat(0x2edbe6ffu);     // DAT_063a53d8, 1e-10f
    const float kEpsilon1e4 = IVP_BuilderBitsToFloat(0x38d1b717u);      // DAT_063a547c, 1e-4f
    const float kEpsilon1e8 = IVP_BuilderBitsToFloat(0x322bcc77u);      // DAT_063a5990, 1e-8f
    const float kEpsilon1e8Below = IVP_BuilderBitsToFloat(0x322bcc76u); // DAT_063a5994, 1e-8f minus one ulp
    const float kEpsilon1e9 = IVP_BuilderBitsToFloat(0x3089705fu);      // DAT_063a5914, 1e-9f
    const float kMinusEpsilon1e6 = IVP_BuilderBitsToFloat(0xb58637bdu); // immediate at 0637a274, -1e-6f

    // .data:DAT_0658b9c8 -- a float in the zero-filled tail of .data with a
    // single XREF (the read at 06378b1e) and no writer, so it is always 0.
    float G_DAT_0658b9c8 = 0.0f;

    // Retail stores 0xffffffff into the next link of an element it has just
    // unlinked from a list, right before deleting it.
    template <class T>
    T* UnlinkedMarker()
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(0xffffffffu));
    }

    // Both pointer pairs below are hashed as 8-byte keys (IVP_Hash key size 8).
    static_assert(sizeof(uintptr_t) * 2 == 8, "edge keys are two 4-byte pointers");
}

// ---------------------------------------------------------------------------
// IVP_Triangle / IVP_Tri_Edge
// ---------------------------------------------------------------------------

// FUN_06378a10 (.text:06378a10-06378a36): set_plane_from_3_points (063480a0)
// with (e0.start, e0.prev.start, e0.next.start), then normize_plane (06348150).
void IVP_Triangle::calc_hesse()
{
    tmp_hesse.set_plane_from_3_points(three_edges[0].start_point, three_edges[0].prev->start_point,
        three_edges[0].next->start_point);
    tmp_hesse.normize_plane();
}

// FUN_06378a40 (.text:06378a40-06378a65).
IVP_Tri_Edge* IVP_Tri_Edge::other_side()
{
    IVP_Triangle* const Other = triangle->other_side;
    if (Other == nullptr)
    {
        return nullptr;
    }
    IVP_Tri_Edge* Edge = &Other->three_edges[0];
    for (int32_t Remaining = 2; Remaining >= 0; --Remaining, ++Edge)
    {
        if (Edge->next->start_point == start_point)
        {
            return Edge;
        }
    }
    return nullptr;
}

// FUN_0637a0e0 (.text:0637a0e0-0637a10d): vtable 063a5998; +0x04..+0x10 = 0,
// the two flag bytes = 0, +0x18 = 0, then REP STOSD of 0x1b zero dwords over
// the three edges (+0x1c..+0x87). tmp_hesse and index are left as allocated.
IVP_Triangle::IVP_Triangle()
    : next(nullptr)
    , prev(nullptr)
    , other_side(nullptr)
    , pierce_partner(nullptr)
    , Field_0x14(0)
    , is_back_side(0)
    , ivp_surface(nullptr)
{
    std::memset(three_edges, 0, sizeof(three_edges));
}

// FUN_0637a130 (vtable store only) / FUN_0637a110 (slot 0: dtor, then
// operator delete when bit 0 of the flag is set).
IVP_Triangle::~IVP_Triangle()
{
}

// ---------------------------------------------------------------------------
// IVP_Poly_Surface
// ---------------------------------------------------------------------------

// FUN_06379b00 (.text:06379b00-06379b07): MOV [ECX+4],[ESP+8]; RET 8.
void IVP_Poly_Surface::set(IVP_Template_Surface* TemplSurface, IVP_Object_Polygon_Tetra* Tetra)
{
    (void)TemplSurface;
    tetra = Tetra;
}

// ---------------------------------------------------------------------------
// P_Sur_2D_Point / P_Sur_2D_Line / P_Sur_2D_Triangle
// ---------------------------------------------------------------------------

// FUN_06378aa0 (.text:06378aa0-06378ab8): REP STOSD 7 zero dwords, +0x10 = arg.
P_Sur_2D_Point::P_Sur_2D_Point(int32_t PointNum)
    : point_num(PointNum)
    , chain_mark(0)
    , line(nullptr)
{
    k[0] = 0.0f;
    k[1] = 0.0f;
    k[2] = 0.0f;
    k[3] = 0.0f;
}

// FUN_06378ac0 (.text:06378ac0-06378af7): REP STOSD 6 zero dwords; start /
// end stored; only when both are set: delta = end - start for x and y.
P_Sur_2D_Line::P_Sur_2D_Line(P_Sur_2D_Point* StartPoint, P_Sur_2D_Point* EndPoint)
    : next(nullptr)
    , prev(nullptr)
    , start_point(StartPoint)
    , end_point(EndPoint)
    , delta_x(0.0f)
    , delta_y(0.0f)
{
    if (StartPoint != nullptr && EndPoint != nullptr)
    {
        delta_x = (float)((double)(EndPoint->k[0]) - StartPoint->k[0]);
        delta_y = (float)((double)(EndPoint->k[1]) - StartPoint->k[1]);
    }
}

// FUN_06378a70 (.text:06378a70-06378a99): five zero dwords, then the three
// point numbers at +0x08 / +0x0c / +0x10.
P_Sur_2D_Triangle::P_Sur_2D_Triangle(int32_t Point0, int32_t Point1, int32_t Point2)
    : next(nullptr)
    , prev(nullptr)
{
    point_nums[0] = Point0;
    point_nums[1] = Point1;
    point_nums[2] = Point2;
}

// FUN_06378c10 (.text:06378c10-06378c29): (p.y - s.y) * dx + (s.x - p.x) * dy,
// returned on the x87 stack.
double P_Sur_2D_Line::hesse_dist_to_point(const P_Sur_2D_Point* Point) const
{
    const double A = ((double)(Point->k[1]) - start_point->k[1]) * delta_x;
    const double B = ((double)(start_point->k[0]) - Point->k[0]) * delta_y;
    return A + B;
}

// FUN_06378b00 (.text:06378b00-06378b35): d = hesse_dist_to_point(p);
// FCOM 1e-10f, TEST AH,0x41, JZ -> d > 1e-10f continues, otherwise return 0;
// then FCOMP DAT_0658b9c8, TEST AH,0x41: EAX = 2, JNZ keeps 2 (d <= value or
// unordered), else 1. DAT_0658b9c8 is always 0 and d > 1e-10f here, so 1 is
// the only value this path returns.
int32_t P_Sur_2D_Line::point_lies_to_the_left(const P_Sur_2D_Point* Point) const
{
    const double Dist = hesse_dist_to_point(Point);
    if (!(Dist > kEpsilon1e10))
    {
        return 0;
    }
    if (!(Dist > G_DAT_0658b9c8))
    {
        return 2;
    }
    return 1;
}

// FUN_06378b40 (.text:06378b40-06378b8e, RET 8).
int32_t P_Sur_2D_Line::has_points(const P_Sur_2D_Point* A, const P_Sur_2D_Point* B) const
{
    if (B == nullptr)
    {
        return (start_point == A || end_point == A) ? 1 : 0;
    }
    if (start_point == A && end_point == B)
    {
        return 1;
    }
    if (end_point == A && start_point == B)
    {
        return 1;
    }
    return 0;
}

// FUN_06378ba0 (.text:06378ba0-06378c08): len2 = dx*dx + dy*dy; FCOM 1e-10f,
// TEST AH,1, JZ -> len2 >= 1e-10f takes the signed distance
// hesse_dist / sqrt(len2) (computed inline, same terms as 06378c10); less or
// unordered returns (s.y - p.y)^2 + (s.x - p.x)^2 instead (no sqrt).
double P_Sur_2D_Line::dist_to_point(const P_Sur_2D_Point* Point) const
{
    const double Len2 = (double)(delta_x) * delta_x + (double)(delta_y) * delta_y;
    if (!(Len2 >= kEpsilon1e10))
    {
        const double A = (double)(start_point->k[0]) - Point->k[0];
        const double B = (double)(start_point->k[1]) - Point->k[1];
        return B * B + A * A;
    }
    const double Hesse = ((double)(Point->k[1]) - start_point->k[1]) * delta_x
        + ((double)(start_point->k[0]) - Point->k[0]) * delta_y;
    return Hesse / std::sqrt(Len2);
}

// FUN_06378c30 (.text:06378c30-06378d00). The dominant axis is x when
// |dx| > 1e-10f, else y when |dy| > 1e-10f, else the result is 0. On that
// axis: sign = +1 when delta > 0 else -1 (FIMUL by the int), v = (p - s) * sign;
// v <= 1e-4f (or unordered) -> 0; then FLD |delta| (rounded to float by the
// FST at 06378c38 / 06378c94), FSUB 1e-4f, FCOMPP against v: TEST AH,1, JZ
// -> v not less -> 0; otherwise 1.
int32_t P_Sur_2D_Line::point_lies_in_interval(const P_Sur_2D_Point* Point) const
{
    int32_t Axis = 0;
    float AbsDelta = std::fabs(delta_x);
    if (!(AbsDelta > kEpsilon1e10))
    {
        AbsDelta = std::fabs(delta_y);
        if (!(AbsDelta > kEpsilon1e10))
        {
            return 0;
        }
        Axis = 1;
    }
    const float Delta = (Axis == 0) ? delta_x : delta_y;
    int32_t Sign = 1;
    if (!(Delta > 0.0f))
    {
        Sign = -1;
    }
    const double V = ((double)(Point->k[Axis]) - start_point->k[Axis]) * Sign;
    if (!(V > kEpsilon1e4))
    {
        return 0;
    }
    const double Limit = (double)(AbsDelta) - kEpsilon1e4;
    if (!(V < Limit))
    {
        return 0;
    }
    return 1;
}

// FUN_06378d10 (.text:06378d10-06378e91). Axis x when |dx| > 1e-8f, else y
// when |dy| > 1e-8f, else Other->point_lies_in_interval(start_point).
// On the axis: delta > 0 -> lo = start, hi = end, else lo = end, hi = start;
// MinV = float(lo + 1e-8f), MaxV = float(hi - 1e-8f); with os / oe the other
// line's start / end coordinate:
//   os <= MinV (or unordered) and oe <= MinV (or unordered) -> 0;
//   os < MaxV (or unordered) -> 1; oe < MaxV (or unordered) -> 1; else 0.
int32_t P_Sur_2D_Line::overlaps_with_line(const P_Sur_2D_Line* Other) const
{
    int32_t Axis;
    if (std::fabs(delta_x) > kEpsilon1e8)
    {
        Axis = 0;
    }
    else if (std::fabs(delta_y) > kEpsilon1e8)
    {
        Axis = 1;
    }
    else
    {
        return Other->point_lies_in_interval(start_point);
    }

    const float Delta = (Axis == 0) ? delta_x : delta_y;
    const P_Sur_2D_Point* Lo;
    const P_Sur_2D_Point* Hi;
    if (Delta > 0.0f)
    {
        Lo = start_point;
        Hi = end_point;
    }
    else
    {
        Lo = end_point;
        Hi = start_point;
    }
    const float MinV = (float)((double)(Lo->k[Axis]) + kEpsilon1e8);
    const float MaxV = (float)((double)(Hi->k[Axis]) - kEpsilon1e8);
    const float OtherEnd = Other->end_point->k[Axis];
    const float OtherStart = Other->start_point->k[Axis];

    if (!(OtherStart > MinV) && !(OtherEnd > MinV))
    {
        return 0;
    }
    if (!(OtherStart >= MaxV))
    {
        return 1;
    }
    if (!(OtherEnd >= MaxV))
    {
        return 1;
    }
    return 0;
}

// FUN_06378ea0 (.text:06378ea0-06378fc7).
//   cr = dy * o.dx - dx * o.dy (kept on the stack; FST rounds a float copy).
//   |cr| < 1e-9f (or unordered): parallel -> |dist_to_point(o.start)| below
//     DAT_063a5994 (or unordered) ? overlaps_with_line(o) != 0 : 0.
//   Otherwise: a shared end point (pointer equality, 4 pairs) -> 0;
//     ex / ey = float(o.start - start); [lo, hi] = [0, cr] when cr > 0, else
//     [cr, 0] (cr as the stored float); t = o.dx*ey - o.dy*ex and
//     u = dx*ey - dy*ex must both lie in [lo, hi] (t < lo or unordered -> 0,
//     t > hi -> 0, same for u), then 1.
int32_t P_Sur_2D_Line::is_crossing_line(const P_Sur_2D_Line* Other) const
{
    const double Cross = (double)(delta_y) * Other->delta_x - (double)(delta_x) * Other->delta_y;
    const float CrossF = (float)(Cross);
    if (!(std::fabs(Cross) >= kEpsilon1e9))
    {
        if (!(std::fabs(dist_to_point(Other->start_point)) >= kEpsilon1e8Below))
        {
            return overlaps_with_line(Other) != 0 ? 1 : 0;
        }
        return 0;
    }

    if (start_point == Other->end_point || start_point == Other->start_point || end_point == Other->start_point
        || end_point == Other->end_point)
    {
        return 0;
    }

    const float Ex = (float)((double)(Other->start_point->k[0]) - start_point->k[0]);
    const float Ey = (float)((double)(Other->start_point->k[1]) - start_point->k[1]);
    float Lo;
    double Hi;
    if (CrossF > 0.0f)
    {
        Lo = 0.0f;
        Hi = CrossF;
    }
    else
    {
        Lo = CrossF;
        Hi = 0.0;
    }

    const double T = (double)(Other->delta_x) * Ey - (double)(Other->delta_y) * Ex;
    if (!(T >= Lo))
    {
        return 0;
    }
    if (T > Hi)
    {
        return 0;
    }
    const double U = (double)(delta_x) * Ey - (double)(delta_y) * Ex;
    if (!(U >= Lo))
    {
        return 0;
    }
    if (U > Hi)
    {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// P_Sur_2D
// ---------------------------------------------------------------------------

// FUN_06378fd0 (.text:06378fd0-06378ffa): all 8 dwords zero, then
// tetra / orig_surface.
P_Sur_2D::P_Sur_2D(IVP_Object_Polygon_Tetra* Tetra, IVP_Template_Surface* OrigSurface)
    : tetra(Tetra)
    , orig_surface(OrigSurface)
    , lines(nullptr)
    , n_lines(0)
    , triangles(nullptr)
    , n_triangles(0)
    , line_array(nullptr)
    , point_array(nullptr)
{
}

// FUN_06379000 (.text:06379000-063790f5): frees line_array / point_array
// (FUN_06343fb0, nulled); deletes every remaining line together with its
// start and end point (the no-op dtor 062a8920 + operator delete each),
// unlinking it first (next = 0xffffffff, n_lines--); then unlinks and
// operator-deletes every 2D triangle. Lines share points, so a non-empty line
// list frees a point twice -- retail only reaches this after
// calc_triangle_representation emptied the list (06379e0d); the error paths
// of make_triangles return without destroying the P_Sur_2D.
P_Sur_2D::~P_Sur_2D()
{
    if (line_array != nullptr)
    {
        free(line_array);
        line_array = nullptr;
    }
    if (point_array != nullptr)
    {
        free(point_array);
        point_array = nullptr;
    }

    while (lines != nullptr)
    {
        P_Sur_2D_Line* const Line = lines;
        delete Line->start_point;
        Line->start_point = nullptr;
        delete Line->end_point;
        Line->end_point = nullptr;

        if (Line->prev != nullptr)
        {
            Line->prev->next = Line->next;
        }
        else
        {
            lines = Line->next;
        }
        if (Line->next != nullptr)
        {
            Line->next->prev = Line->prev;
        }
        Line->next = UnlinkedMarker<P_Sur_2D_Line>();
        --n_lines;
        delete Line;
    }

    while (triangles != nullptr)
    {
        P_Sur_2D_Triangle* const Triangle = triangles;
        if (Triangle->prev != nullptr)
        {
            Triangle->prev->next = Triangle->next;
        }
        else
        {
            triangles = Triangle->next;
        }
        if (Triangle->next != nullptr)
        {
            Triangle->next->prev = Triangle->prev;
        }
        Triangle->next = UnlinkedMarker<P_Sur_2D_Triangle>();
        --n_triangles;
        delete Triangle;
    }
}

// FUN_06379100 (.text:06379100-063793c6).
// Projection axis = argmax |normal| (strictly greater replaces, x first);
// (U, V) = axis 0: (2, 1), axis 1: (2, 0), axis 2: (1, 0); a 2D point is
// x = coord[V], y = coord[U], z = 0. flip = normal[axis] < 0 (or unordered),
// inverted for axis 1. The retail default case of the axis switch (writes to
// address 0) is unreachable: the axis is always 0, 1 or 2.
// Template lines are walked from the last to the first; each one yields a
// directed line start -> end (start = p[revert], end = p[1 - revert], revert
// flipped by flip), its points are created once per point number (and their
// coordinates rewritten on every use), the line is pushed at the head of the
// line list, recorded in line_array[k] and becomes its start point's line.
const char* P_Sur_2D::calc_line_representation()
{
    IVP_Template_Surface* const Surface = orig_surface;
    if (Surface == nullptr)
    {
        return "calc_line_representation: no orig_surface specified!\n";
    }
    IVP_Object_Polygon_Tetra* const Tetra = tetra;

    int32_t Axis = 0;
    float MaxAbs = std::fabs(Surface->normal[0]);
    const float AbsY = std::fabs(Surface->normal[1]);
    if (AbsY > MaxAbs)
    {
        Axis = 1;
        MaxAbs = AbsY;
    }
    if (std::fabs(Surface->normal[2]) > MaxAbs)
    {
        Axis = 2;
    }

    int32_t U = 0; // local_20
    int32_t V = 0; // local_1c
    switch (Axis)
    {
    case 0:
        V = 1;
        U = 2;
        break;
    case 1:
        V = 0;
        U = 2;
        break;
    case 2:
        V = 0;
        U = 1;
        break;
    }

    int32_t Flip = 0;
    if (!(Surface->normal[Axis] >= 0.0f))
    {
        Flip = 1;
    }
    if (Axis == 1)
    {
        Flip = 1 - Flip;
    }

    P_Sur_2D_Point** const Lookup = static_cast<P_Sur_2D_Point**>(calloc(static_cast<size_t>(Tetra->n_points), 4));
    if (line_array != nullptr)
    {
        free(line_array);
        line_array = nullptr;
    }
    line_array = static_cast<P_Sur_2D_Line**>(calloc(static_cast<size_t>(Surface->n_lines), 4));
    if (point_array != nullptr)
    {
        free(point_array);
        point_array = nullptr;
    }
    point_array = static_cast<P_Sur_2D_Point**>(calloc(static_cast<size_t>(Surface->n_lines), 4));

    int32_t NextPoint = 0;
    for (int32_t K = Surface->n_lines - 1; K >= 0; --K)
    {
        const uint8_t* const LineBytes =
            reinterpret_cast<const uint8_t*>(&Tetra->template_polygon->lines[Surface->lines[K]]);
        int32_t Revert = static_cast<signed char>(Surface->revert_line[K]);
        if (Flip != 0)
        {
            Revert = 1 - Revert;
        }
        // MOV SI,[line + revert*2] / MOV DI,[line - revert*2 + 2].
        const int32_t StartNum = *reinterpret_cast<const uint16_t*>(LineBytes + Revert * 2);
        const int32_t EndNum = *reinterpret_cast<const uint16_t*>(LineBytes - Revert * 2 + 2);

        P_Sur_2D_Point* Start = Lookup[StartNum];
        if (Start == nullptr)
        {
            Start = new P_Sur_2D_Point(StartNum);
            Lookup[StartNum] = Start;
            point_array[NextPoint] = Start;
            ++NextPoint;
        }
        Start->k[0] = Tetra->points[StartNum].k[V];
        Start->k[2] = 0.0f;
        Start->k[1] = Tetra->points[StartNum].k[U];

        P_Sur_2D_Point* End = Lookup[EndNum];
        if (End == nullptr)
        {
            End = new P_Sur_2D_Point(EndNum);
            Lookup[EndNum] = End;
            point_array[NextPoint] = End;
            ++NextPoint;
        }
        End->k[0] = Tetra->points[EndNum].k[V];
        End->k[2] = 0.0f;
        End->k[1] = Tetra->points[EndNum].k[U];

        P_Sur_2D_Line* const Line = new P_Sur_2D_Line(Start, End);
        Start->line = Line;
        Line->next = lines;
        if (lines != nullptr)
        {
            lines->prev = Line;
        }
        Line->prev = nullptr;
        ++n_lines;
        lines = Line;
        line_array[K] = Line;
    }

    if (Lookup != nullptr)
    {
        free(Lookup);
    }
    return nullptr;
}

// FUN_063793d0 (.text:063793d0-063793f6).
int32_t P_Sur_2D::mark_point_chain(P_Sur_2D_Point* Point)
{
    int32_t Count = 0;
    while (Point != nullptr && Point->chain_mark == 0)
    {
        Point->chain_mark = 1;
        Point = Point->line->end_point;
        ++Count;
    }
    return Count;
}

// FUN_06379400 (.text:06379400-0637945a): a = P2 - P0, b = P2 - P1, all on
// the x87 stack; sum = ((((bz^2 + by^2) + bx^2) + az^2) + ay^2) + ax^2.
double P_Sur_2D::sum_quad_dists(const IVP_U_Point* P0, const IVP_U_Point* P1, const IVP_U_Point* P2)
{
    const double Ax = (double)(P2->k[0]) - P0->k[0];
    const double Ay = (double)(P2->k[1]) - P0->k[1];
    const double Az = (double)(P2->k[2]) - P0->k[2];
    const double Bx = (double)(P2->k[0]) - P1->k[0];
    const double By = (double)(P2->k[1]) - P1->k[1];
    const double Bz = (double)(P2->k[2]) - P1->k[2];
    double Sum = Bz * Bz + By * By;
    Sum += Bx * Bx;
    Sum += Az * Az;
    Sum += Ay * Ay;
    Sum += Ax * Ax;
    return Sum;
}

// FUN_06379460 (.text:06379460-063799ac): ear clipping of the outline.
// Each pass (at most 101; the 102nd fails with "Cannot convert" after
// printing every remaining line's start point) takes the head line as the
// base, ranks every line by sum_quad_dists(base.start, base.end, line.start)
// in an IVP_U_Min_Hash and tries the candidates nearest first: the apex is
// the candidate's start point; it must differ from base.end and lie strictly
// left of the base; the two new sides (apex -> base.start, base.end -> apex)
// must not cross any other line, and no line start may lie strictly left of
// the base and both sides. An accepted ear becomes a 2D triangle (base.start,
// apex, base.end), the base line is deleted, a line equal to a new side is
// deleted (it cancels) or else the side is added at the head.
const char* P_Sur_2D::calc_triangle_representation()
{
    // Retail reads lines->start_point with no null check (.text:06379482-06379491);
    // the returned count is not used.
    mark_point_chain(lines->start_point);

    IVP_U_Min_Hash MinHash(0x10);
    P_Sur_2D_Line* SideA = nullptr; // local_3c: apex -> base.start
    P_Sur_2D_Line* SideB = nullptr; // local_38: base.end -> apex
    int32_t Pass = 0;               // local_2c

    for (P_Sur_2D_Line* Base = lines; Base != nullptr; Base = lines)
    {
        const int32_t ThisPass = Pass;
        ++Pass;
        if (ThisPass > 100)
        {
            // LAB_06379949.
            for (P_Sur_2D_Line* Line = lines; Line != nullptr; Line = Line->next)
            {
                const P_Sur_2D_Point* const Start = Line->start_point;
                printf("  %f %f %f\n", (double)(Start->k[0]), (double)(Start->k[1]),
                    (double)(Start->k[2]));
            }
            return "Cannot convert";
        }

        while (MinHash.find_min_elem() != nullptr)
        {
            MinHash.remove_min();
        }

        P_Sur_2D_Point* const BaseStart = Base->start_point; // local_4c
        P_Sur_2D_Point* const BaseEnd = Base->end_point;     // local_50
        for (P_Sur_2D_Line* Line = lines; Line != nullptr; Line = Line->next)
        {
            MinHash.add(Line, (float)(sum_quad_dists(BaseStart, BaseEnd, Line->start_point)));
        }

        for (P_Sur_2D_Line* Candidate = static_cast<P_Sur_2D_Line*>(MinHash.find_min_elem()); Candidate != nullptr;
             Candidate = static_cast<P_Sur_2D_Line*>(MinHash.find_min_elem()))
        {
            P_Sur_2D_Point* const Apex = Candidate->start_point; // local_48
            if (Candidate != Base && Apex != BaseEnd && Base->point_lies_to_the_left(Apex) != 0)
            {
                delete SideA;
                SideA = new P_Sur_2D_Line(Apex, BaseStart);
                delete SideB;
                SideB = new P_Sur_2D_Line(BaseEnd, Apex);

                bool bCrossing = false;
                for (P_Sur_2D_Line* Line = lines; Line != nullptr; Line = Line->next)
                {
                    if (Line->has_points(Apex, BaseStart) == 0 && Line->is_crossing_line(SideA) != 0)
                    {
                        bCrossing = true;
                        break;
                    }
                    if (Line->has_points(BaseEnd, Apex) == 0 && Line->is_crossing_line(SideB) != 0)
                    {
                        bCrossing = true;
                        break;
                    }
                }

                bool bReject = bCrossing;
                bool bNoMatch = false;
                if (bCrossing)
                {
                    // LAB_0637967a: the candidate itself is still the minimum here,
                    // so this test always finds an element.
                    bNoMatch = (MinHash.find_min_elem() == nullptr);
                }
                else
                {
                    for (P_Sur_2D_Line* Line = lines; Line != nullptr; Line = Line->next)
                    {
                        P_Sur_2D_Point* const Point = Line->start_point;
                        if (Base->point_lies_to_the_left(Point) != 0 && SideB->point_lies_to_the_left(Point) != 0
                            && SideA->point_lies_to_the_left(Point) != 0)
                        {
                            bReject = true;
                            // LAB_0637968e.
                            bNoMatch = (Candidate->next == nullptr);
                            break;
                        }
                    }
                }

                if (bNoMatch)
                {
                    // LAB_06379697: printf, then MOV [0],0.
                    printf("Couldn't find a matching point to baseline!\n");
                    // VERY STRANGE, REPRODUCED ON PURPOSE: retail's IVP assert writes to address 0.
                    *static_cast<volatile int32_t*>(nullptr) = 0;
                }

                if (!bReject)
                {
                    // LAB_063796df: accept the ear.
                    P_Sur_2D_Triangle* const Triangle =
                        new P_Sur_2D_Triangle(BaseStart->point_num, Apex->point_num, BaseEnd->point_num);
                    Triangle->next = triangles;
                    if (triangles != nullptr)
                    {
                        triangles->prev = Triangle;
                    }
                    Triangle->prev = nullptr;
                    ++n_triangles;
                    triangles = Triangle;

                    if (Base->prev != nullptr)
                    {
                        Base->prev->next = Base->next;
                    }
                    else
                    {
                        lines = Base->next;
                    }
                    if (Base->next != nullptr)
                    {
                        Base->next->prev = Base->prev;
                    }
                    Base->next = UnlinkedMarker<P_Sur_2D_Line>();
                    --n_lines;
                    delete Base;

                    bool bFoundSideA = false;
                    bool bFoundSideB = false;
                    P_Sur_2D_Line* Line = lines;
                    while (Line != nullptr)
                    {
                        P_Sur_2D_Line* const Next = Line->next;
                        bool bRemove = false;
                        if (Line->has_points(Apex, BaseStart) != 0)
                        {
                            bRemove = true;
                            bFoundSideA = true;
                        }
                        else if (Line->has_points(Apex, BaseEnd) != 0)
                        {
                            bRemove = true;
                            bFoundSideB = true;
                        }
                        if (bRemove)
                        {
                            if (Line->prev != nullptr)
                            {
                                Line->prev->next = Line->next;
                            }
                            else
                            {
                                lines = Line->next;
                            }
                            if (Line->next != nullptr)
                            {
                                Line->next->prev = Line->prev;
                            }
                            Line->next = UnlinkedMarker<P_Sur_2D_Line>();
                            --n_lines;
                            delete Line;
                        }
                        Line = Next;
                    }

                    if (!bFoundSideA)
                    {
                        P_Sur_2D_Line* const NewLine = new P_Sur_2D_Line(BaseStart, Apex);
                        NewLine->next = lines;
                        if (lines != nullptr)
                        {
                            lines->prev = NewLine;
                        }
                        NewLine->prev = nullptr;
                        lines = NewLine;
                        ++n_lines;
                    }
                    if (!bFoundSideB)
                    {
                        P_Sur_2D_Line* const NewLine = new P_Sur_2D_Line(Apex, BaseEnd);
                        NewLine->next = lines;
                        if (lines != nullptr)
                        {
                            lines->prev = NewLine;
                        }
                        NewLine->prev = nullptr;
                        lines = NewLine;
                        ++n_lines;
                    }
                    break;
                }
            }

            // LAB_063796af.
            MinHash.remove_min();
        }
    }

    // LAB_063798cd / LAB_063798fd.
    delete SideA;
    delete SideB;
    for (int32_t I = orig_surface->n_lines - 1; I >= 0; --I)
    {
        delete point_array[I];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// IVP_Object_Polygon_Tetra
// ---------------------------------------------------------------------------

// FUN_06379a10 (.text:06379a10-06379afb): REP STOSD 0x14 zero dwords;
// template_polygon = Templ; points = FUN_06343f80 (calloc) of n_points * 0x18,
// each point's x / y / z copied from the template point and l_tetras = this;
// surfaces = calloc of n_surfaces * 0xc, each set(&templ->surfaces[i], this)
// (06379b00); the last store clears template_polygon (both paths), the caller
// (0636dab0) sets it again.
IVP_Object_Polygon_Tetra::IVP_Object_Polygon_Tetra(IVP_Template_Polygon* Templ)
    : template_polygon(Templ)
    , edge_hash(nullptr)
    , triangles(nullptr)
    , n_triangles(0)
    , points(nullptr)
    , n_points(0)
    , surfaces(nullptr)
    , n_surfaces(0)
    , Field_0x48(nullptr)
    , Field_0x4c(0)
{
    std::memset(Field_0x04, 0, sizeof(Field_0x04));
    std::memset(Field_0x1c, 0, sizeof(Field_0x1c));

    n_points = Templ->n_points;
    points = static_cast<IVP_Poly_Point*>(calloc(static_cast<size_t>(n_points), 0x18));
    for (int32_t I = 0; I < n_points; ++I)
    {
        const IVP_U_Float_Point& Source = Templ->points[I];
        IVP_Poly_Point& Point = points[I];
        Point.k[0] = Source.X;
        Point.k[2] = Source.Z;
        Point.k[1] = Source.Y;
        Point.l_tetras = this;
    }

    n_surfaces = Templ->n_surfaces;
    surfaces = static_cast<IVP_Poly_Surface*>(calloc(static_cast<size_t>(n_surfaces), 0xc));
    for (int32_t I = 0; I < n_surfaces; ++I)
    {
        surfaces[I].set(&Templ->surfaces[I], this);
    }
    template_polygon = nullptr;
}

// FUN_06379b10 (.text:06379b10-06379b47): free_triangles, then free (FUN_06343fb0)
// surfaces and points when set, nulling each.
IVP_Object_Polygon_Tetra::~IVP_Object_Polygon_Tetra()
{
    free_triangles();
    if (surfaces != nullptr)
    {
        free(surfaces);
        surfaces = nullptr;
    }
    if (points != nullptr)
    {
        free(points);
        points = nullptr;
    }
}

// FUN_06379ff0 (.text:06379ff0-0637a05d): unlinks the head triangle
// (next = 0xffffffff, n_triangles--) and deletes it through vtable slot 0
// until the list is empty; then operator-deletes the Field_0x48 chain (next
// pointer at +0x18) and clears it.
void IVP_Object_Polygon_Tetra::free_triangles()
{
    while (triangles != nullptr)
    {
        IVP_Triangle* const Triangle = triangles;
        if (Triangle->prev != nullptr)
        {
            Triangle->prev->next = Triangle->next;
        }
        else
        {
            triangles = Triangle->next;
        }
        if (Triangle->next != nullptr)
        {
            Triangle->next->prev = Triangle->prev;
        }
        Triangle->next = UnlinkedMarker<IVP_Triangle>();
        --n_triangles;
        delete Triangle;
    }

    void* Node = Field_0x48;
    while (Node != nullptr)
    {
        void* const Next = *reinterpret_cast<void**>(static_cast<uint8_t*>(Node) + 0x18);
        ::operator delete(Node);
        Node = Next;
    }
    Field_0x48 = nullptr;
}

// FUN_063799b0 (.text:063799b0-06379a06): key = {min(A, B), max(A, B)}
// (unsigned pointer compare); a stored edge -> both opposite links set to
// each other, otherwise Edge is stored under the key.
void IVP_Object_Polygon_Tetra::link_edge(IVP_Hash* Hash, IVP_Poly_Point* A, IVP_Poly_Point* B, IVP_Tri_Edge* Edge)
{
    uintptr_t Key[2];
    const uintptr_t KeyA = reinterpret_cast<uintptr_t>(A);
    const uintptr_t KeyB = reinterpret_cast<uintptr_t>(B);
    if (KeyA < KeyB)
    {
        Key[0] = KeyA;
        Key[1] = KeyB;
    }
    else
    {
        Key[0] = KeyB;
        Key[1] = KeyA;
    }
    IVP_Tri_Edge* const Found = reinterpret_cast<IVP_Tri_Edge*>(static_cast<uintptr_t>(Hash->Find(Key)));
    if (Found == nullptr)
    {
        Hash->Add(Key, static_cast<unsigned>(reinterpret_cast<uintptr_t>(Edge)));
        return;
    }
    Edge->opposite = Found;
    Found->opposite = Edge;
}

// FUN_06379b50 (.text:06379b50-06379fe8).
// Edge hash (point pair -> edge) of (n_points - 2) * 6 buckets. For every
// template surface in order: a P_Sur_2D, its lines and its ear-clipped 2D
// triangles (an error prints "make_triangles:...: %s\n" and returns with the
// P_Sur_2D and the hash leaked, as retail); every 2D triangle (p0, p1, p2)
// becomes a front IVP_Triangle at the head of the list over the tetra points:
// edges p2 -> s -> t -> p2 where (s, t) = (p1, p0), swapped when the normal
// cross(p1 - p2, p0 - p2) points away from the surface normal (dot < 0 or
// unordered); its surface is surfaces[template surface index]. Then each
// front triangle gets its back side copy (reversed edges, is_back_side = 1),
// whose edges are linked with the front edges and registered in edge_hash.
const char* IVP_Object_Polygon_Tetra::make_triangles()
{
    IVP_Template_Polygon* const Templ = template_polygon;
    int32_t EdgeSlots = 0; // local_4c: += 3 per triangle, never read
    IVP_Hash* const Hash = new IVP_Hash((Templ->n_points - 2) * 6, 8, 0);

    IVP_Template_Surface* TemplSurface = Templ->surfaces;
    for (int32_t Remaining = Templ->n_surfaces - 1; Remaining >= 0; --Remaining, ++TemplSurface)
    {
        P_Sur_2D* const Sur2D = new P_Sur_2D(this, TemplSurface);
        const char* Error = Sur2D->calc_line_representation();
        if (Error != nullptr)
        {
            printf("make_triangles:calc_line_representation: %s\n", Error);
            return "No 2d representation";
        }
        Error = Sur2D->calc_triangle_representation();
        if (Error != nullptr)
        {
            printf("make_triangles:calc_triangle_representation: %s\n", Error);
            return "no 3d representation";
        }

        for (P_Sur_2D_Triangle* Tri2D = Sur2D->triangles; Tri2D != nullptr; Tri2D = Tri2D->next)
        {
            IVP_Poly_Point* const P2 = &points[Tri2D->point_nums[2]];
            IVP_Poly_Point* const P1 = &points[Tri2D->point_nums[1]];
            IVP_Poly_Point* const P0 = &points[Tri2D->point_nums[0]];
            IVP_Poly_Point* Second = P1; // ESI / local_5c
            IVP_Poly_Point* Third = P0;  // local_58

            float A[3];
            A[0] = (float)((double)(P1->k[0]) - P2->k[0]);
            A[1] = (float)((double)(P1->k[1]) - P2->k[1]);
            A[2] = (float)((double)(P1->k[2]) - P2->k[2]);
            float B[3];
            B[0] = (float)((double)(P0->k[0]) - P2->k[0]);
            B[1] = (float)((double)(P0->k[1]) - P2->k[1]);
            B[2] = (float)((double)(P0->k[2]) - P2->k[2]);
            float Normal[3];
            IVP_BuilderCrossProduct(Normal, A, B);
            double Dot = (double)(Normal[1]) * TemplSurface->normal[1]
                + (double)(Normal[2]) * TemplSurface->normal[2];
            Dot += (double)(Normal[0]) * TemplSurface->normal[0];
            if (!(Dot >= 0.0))
            {
                Third = P1;
                Second = P0;
            }

            IVP_Triangle* const Triangle = new IVP_Triangle();
            Triangle->next = triangles;
            if (triangles != nullptr)
            {
                triangles->prev = Triangle;
            }
            Triangle->prev = nullptr;
            ++n_triangles;
            triangles = Triangle;
            Triangle->Field_0x14 = 1;
            Triangle->ivp_surface = &surfaces[TemplSurface->get_surface_index()];
            EdgeSlots += 3;
            Triangle->is_back_side = 0;

            IVP_Tri_Edge* const E0 = &Triangle->three_edges[0];
            IVP_Tri_Edge* const E1 = &Triangle->three_edges[1];
            IVP_Tri_Edge* const E2 = &Triangle->three_edges[2];
            E0->start_point = P2;
            E0->triangle = Triangle;
            E0->next = E1;
            E0->prev = E2;
            E0->behind = nullptr;
            link_edge(Hash, P2, Second, E0);

            E1->start_point = Second;
            E1->prev = E0;
            E1->triangle = Triangle;
            E1->next = E2;
            E1->behind = nullptr;
            link_edge(Hash, Second, Third, E1);

            E2->next = E0;
            E2->start_point = Third;
            E2->triangle = Triangle;
            E2->prev = E1;
            E2->behind = nullptr;
            link_edge(Hash, Third, P2, E2);
        }

        delete Sur2D;
    }
    (void)EdgeSlots;
    delete Hash;

    for (IVP_Triangle* Triangle = triangles; Triangle != nullptr; Triangle = Triangle->next)
    {
        IVP_Triangle* const Other = new IVP_Triangle();
        Other->next = triangles;
        if (triangles != nullptr)
        {
            triangles->prev = Other;
        }
        Other->prev = nullptr;
        ++n_triangles;
        triangles = Other;
        Other->ivp_surface = nullptr;
        Other->Field_0x14 = 1;
        Other->is_back_side = 1;
        Other->other_side = Triangle;
        Triangle->other_side = Other;

        for (int32_t K = 3; K >= 1; --K)
        {
            IVP_Tri_Edge* const OtherEdge = &Other->three_edges[K - 1];
            IVP_Tri_Edge* const FrontEdge = &Triangle->three_edges[K - 1];
            OtherEdge->start_point = FrontEdge->next->start_point;
            OtherEdge->triangle = Other;
            OtherEdge->next = &Other->three_edges[(K + 1) % 3];
            OtherEdge->prev = &Other->three_edges[K % 3];
            OtherEdge->behind = FrontEdge->opposite;
            IVP_Tri_Edge* const Across = OtherEdge->other_side()->opposite->other_side();
            if (Across != nullptr)
            {
                OtherEdge->opposite = Across;
                Across->opposite = OtherEdge;
            }
            FrontEdge->opposite->behind = OtherEdge;
        }
        for (int32_t K = 2; K >= 0; --K)
        {
            add_edge_to_hash(&Other->three_edges[K]);
        }
    }
    return nullptr;
}

// FUN_0637a060 (.text:0637a060-0637a088): for each of the three edges:
// triangle = Triangle, behind = the edge itself, opposite = other_side().
void IVP_Object_Polygon_Tetra::init_tri_edges(IVP_Triangle* Triangle)
{
    IVP_Tri_Edge* Edge = &Triangle->three_edges[0];
    for (int32_t I = 0; I < 3; ++I, ++Edge)
    {
        Edge->triangle = Triangle;
        Edge->behind = Edge;
        Edge->opposite = Edge->other_side();
    }
}

// FUN_0637a090 (.text:0637a090-0637a0da): key = {min, max} of the edge's start
// point and its next edge's start point (unsigned compare, JBE keeps the
// order); stored in edge_hash only when the key is absent.
void IVP_Object_Polygon_Tetra::add_edge_to_hash(IVP_Tri_Edge* Edge)
{
    uintptr_t Key[2];
    Key[0] = reinterpret_cast<uintptr_t>(Edge->start_point);
    Key[1] = reinterpret_cast<uintptr_t>(Edge->next->start_point);
    if (Key[0] > Key[1])
    {
        const uintptr_t Swap = Key[0];
        Key[0] = Key[1];
        Key[1] = Swap;
    }
    if (edge_hash->Find(Key) == 0)
    {
        edge_hash->Add(Key, static_cast<unsigned>(reinterpret_cast<uintptr_t>(Edge)));
    }
}

// FUN_0637a140 (.text:0637a140-0637a23a): front triangle P1 -> P2 -> P3 and
// its reverse P2 -> P1 -> P3 (array order P2, P3, P1 linked e0 -> e2 -> e1),
// other_side linked both ways, init_tri_edges and calc_hesse on both, then
// every edge's start_point_index = its start point's compact_index (three steps
// along next from e0, front then back). Returns the front triangle.
IVP_Triangle* IVP_Object_Polygon_Tetra::make_double_triangle(IVP_Poly_Point* P1, IVP_Poly_Point* P2, IVP_Poly_Point* P3)
{
    IVP_Triangle* const Front = new IVP_Triangle();
    IVP_Triangle* const Back = new IVP_Triangle();

    IVP_Tri_Edge* const F0 = &Front->three_edges[0];
    IVP_Tri_Edge* const F1 = &Front->three_edges[1];
    IVP_Tri_Edge* const F2 = &Front->three_edges[2];
    F0->start_point = P1;
    F0->triangle = Front;
    F0->next = F1;
    F0->prev = F2;
    F1->next = F2;
    F1->start_point = P2;
    F1->prev = F0;
    F1->triangle = Front;
    F2->prev = F1;
    F2->start_point = P3;
    F2->next = F0;
    F2->triangle = Front;

    IVP_Tri_Edge* const B0 = &Back->three_edges[0];
    IVP_Tri_Edge* const B1 = &Back->three_edges[1];
    IVP_Tri_Edge* const B2 = &Back->three_edges[2];
    B0->start_point = P2;
    B0->triangle = Back;
    B0->next = B2;
    B0->prev = B1;
    B1->next = B0;
    B1->prev = B2;
    B1->start_point = P3;
    B1->triangle = Back;
    B2->next = B1;
    B2->prev = B0;
    B2->start_point = P1;
    B2->triangle = Back;

    Front->other_side = Back;
    Back->other_side = Front;
    init_tri_edges(Front);
    init_tri_edges(Back);
    Front->calc_hesse();
    Back->calc_hesse();

    IVP_Tri_Edge* Edge = F0;
    for (int32_t I = 0; I < 3; ++I)
    {
        Edge->start_point_index = Edge->start_point->compact_index;
        Edge = Edge->next;
    }
    Edge = B0;
    for (int32_t I = 0; I < 3; ++I)
    {
        Edge->start_point_index = Edge->start_point->compact_index;
        Edge = Edge->next;
    }
    return Front;
}

// FUN_0637a240 (.text:0637a240-0637a2d9): clears every pierce_partner; then
// every front triangle without a partner takes the front triangle whose
// hesse normal gives the smallest dot product below -1e-6f
// ((z*z' + y*y') + x*x' on the stack; the running minimum is stored as a
// float; a NaN dot also replaces it, FCOM + TEST AH,1 + JZ) and the two are
// paired both ways. With no such triangle the partner is null and the
// reverse store writes through it -- retail crashes there the same way.
void IVP_Object_Polygon_Tetra::pair_pierce_triangles()
{
    for (IVP_Triangle* Triangle = triangles; Triangle != nullptr; Triangle = Triangle->next)
    {
        Triangle->pierce_partner = nullptr;
    }
    for (IVP_Triangle* Triangle = triangles; Triangle != nullptr; Triangle = Triangle->next)
    {
        if (Triangle->is_back_side != 0 || Triangle->pierce_partner != nullptr)
        {
            continue;
        }
        IVP_Triangle* Best = nullptr;
        float MinDot = kMinusEpsilon1e6;
        for (IVP_Triangle* Candidate = triangles; Candidate != nullptr; Candidate = Candidate->next)
        {
            if (Candidate->is_back_side != 0)
            {
                continue;
            }
            double Dot = (double)(Candidate->tmp_hesse.k[2]) * Triangle->tmp_hesse.k[2]
                + (double)(Candidate->tmp_hesse.k[1]) * Triangle->tmp_hesse.k[1];
            Dot += (double)(Candidate->tmp_hesse.k[0]) * Triangle->tmp_hesse.k[0];
            if (!(Dot >= MinDot))
            {
                MinDot = (float)(Dot);
                Best = Candidate;
            }
        }
        Triangle->pierce_partner = Best;
        Best->pierce_partner = Triangle;
    }
}
