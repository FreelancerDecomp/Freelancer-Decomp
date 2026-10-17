// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_surface_builder_pointsoup.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "ivp_builder_math.h"
#include "ivp_point_hash.h"
#include "ivp_surface_builder_polygon_convex.h"
#include "ivp_template_polygon.h"
#include "../ivp_utility/ivp_hash.h"
#include "../ivp_utility/ivp_vhash.h"
#include "../ivp_utility/ivp_message_router.h"

#include "../ivp_utility/ivp_malloc.h" // IVP's 16-byte aligned block allocator and its release (FUN_06343fc0 / FUN_06343ff0)

extern "C" {
#include "qhull_a.h"
}

// ============================================================================
// Common.dll .text:0636b770-0636c9f2 (2026-10-08, claude-code:cmn-ivp4b).
//
// IVP debug router. The three "***" messages go through G_IVPMessageRouter
// (DAT_0658c730, ivp_message_router.h) exactly as retail calls it; its
// category levels are never set, so retail never prints them.
//
// qhull error sink. Retail calls qh_new_qhull (063784b0) with outfile and
// errfile NULL (0636c40c / 0636c483). Its qhull reports errors through
// FUN_06356960 instead of fprintf(qh ferr), and qh_errexit (06378600) skips
// the whole report when qh ferr is NULL; src/qhull/vendor does the same.
// ============================================================================

namespace
{
    const float kDegenerateTriangle = IVP_BuilderBitsToFloat(0x358637bdu); // DAT_063a5610, 1e-6f
    const float kJoggleStart = IVP_BuilderBitsToFloat(0x2b8cbcccu);        // immediate at 0636c3d5, 1e-12f
    const float kJoggleAdd = IVP_BuilderBitsToFloat(0x2b8cbcccu);          // DAT_063a5494, 1e-12f
    const float kJoggleScale = IVP_BuilderBitsToFloat(0x3f99999au);        // DAT_063a5668, 1.2f
    const float kJoggleLimit = IVP_BuilderBitsToFloat(0x3ca3d70au);        // DAT_063a5908, 0.02f
    const float kMinusOne = IVP_BuilderBitsToFloat(0xbf800000u);           // DAT_063a5498 / immediates, -1.0f
    const double kSmallAreaFactor = IVP_BuilderBitsToDouble(0x3f747ae140000000ull); // DAT_063a5900 (double), 0.005f widened

    // Point hash of the pointsoup builder, vtable .rdata:063a590c
    // {0636cfe0 compare, 0636c7c0 scalar deleting dtor}. Built on the stack
    // by 0636c2c6 / 0636c61f as FUN_063522a0(0x400) + vtable store and
    // destroyed by the non-deleting 06377e50 (vtable store, JMP FUN_063522f0).
    // The retail class name is not in the binary.
    class FPointsoupPointHash : public IVP_VHash
    {
    public:
        explicit FPointsoupPointHash(int Size)
            : IVP_VHash(Size)
        {
        }

        // FUN_0636cfe0.
        int Compare(void* Elem0, void* Elem1) override
        {
            return IVP_PointHashCompare(Elem0, Elem1);
        }

        // FUN_0636c7c0 -> FUN_06377e50.
        ~FPointsoupPointHash() override
        {
        }
    };

    // FCOMP + FNSTSW + TEST AH,0x40: C3 is set for equal AND for unordered.
    bool X87Equal(float A, float B)
    {
        return !(A < B) && !(A > B);
    }
}

IVP_Compact_Ledge* GPointsoupTriangleTemplate = nullptr;

// ---------------------------------------------------------------------------
// IVP_SurMan_PS_Plane
// ---------------------------------------------------------------------------

// FUN_0636bce0 (.text:0636bce0-0636bdbc). p0 = points[0] (read before the
// count test). For i = 0..n-1 with next = points[(i + 1) % n]:
// A = float(next - p_i), B = float(p_i - p0), C = 0628bed0 cross(A, B)
// (float components), sum = float(((C.z*h.z + C.x*h.x) + C.y*h.y) + sum).
float IVP_SurMan_PS_Plane::get_area_size() const
{
    const IVP_U_Float_Point* const P0 = points.elems[0];
    const int32_t Count = points.n_elems;
    float Sum = 0.0f;
    for (int32_t I = 0; I < Count;)
    {
        const IVP_U_Float_Point* const Pi = points.elems[I];
        ++I;
        const IVP_U_Float_Point* const Pn = points.elems[I % Count];
        float A[3];
        A[0] = (float)((double)(Pn->X) - Pi->X);
        A[1] = (float)((double)(Pn->Y) - Pi->Y);
        A[2] = (float)((double)(Pn->Z) - Pi->Z);
        float B[3];
        B[0] = (float)((double)(Pi->X) - P0->X);
        B[1] = (float)((double)(Pi->Y) - P0->Y);
        B[2] = (float)((double)(Pi->Z) - P0->Z);
        float C[3];
        IVP_BuilderCrossProduct(C, A, B);
        double Dot = (double)(C[2]) * hesse[2] + (double)(C[0]) * hesse[0];
        Dot += (double)(C[1]) * hesse[1];
        Sum = (float)(Dot + Sum);
    }
    return Sum;
}

// FUN_0636bdc0 (.text:0636bdc0-0636be1e): sum += (dz^2 + dy^2) + dx^2 with
// d = p_i - p_(i+1)%n, all on the x87 stack starting from 0.0f.
double IVP_SurMan_PS_Plane::get_qlen_of_all_edges() const
{
    double Sum = 0.0;
    const int32_t Count = points.n_elems;
    for (int32_t I = 0; I < Count;)
    {
        const IVP_U_Float_Point* const Pi = points.elems[I];
        ++I;
        const IVP_U_Float_Point* const Pn = points.elems[I % Count];
        const double Dx = (double)(Pi->X) - Pn->X;
        const double Dy = (double)(Pi->Y) - Pn->Y;
        const double Dz = (double)(Pi->Z) - Pn->Z;
        Sum += (Dz * Dz + Dy * Dy) + Dx * Dx;
    }
    return Sum;
}

// ---------------------------------------------------------------------------
// IVP_SurfaceBuilder_Pointsoup
// ---------------------------------------------------------------------------

// FUN_0636b770 (.text:0636b770-0636b7bd): the index of the first point whose
// x, y and z compare equal (FCOMP, TEST AH,0x40: equal or unordered), or 0
// when there is none -- index 0 and "not found" are the same answer.
int32_t IVP_SurfaceBuilder_Pointsoup::point_index(const IVP_U_Float_Point* Array, int32_t Count, const IVP_U_Float_Point* Point)
{
    for (int32_t I = 0; I < Count; ++I)
    {
        if (X87Equal(Array[I].X, Point->X) && X87Equal(Array[I].Y, Point->Y) && X87Equal(Array[I].Z, Point->Z))
        {
            return I;
        }
    }
    return 0;
}

// FUN_0636b7c0 (.text:0636b7c0-0636b826): the first line (ushort pair) equal
// to (A, B) -> *Revert = 0, or to (B, A) -> *Revert = 1; returns its index,
// or 0 with *Revert untouched when there is none.
int32_t IVP_SurfaceBuilder_Pointsoup::line_index(const void* Lines, int32_t Count, int32_t A, int32_t B, uint8_t* Revert)
{
    const IVP_Template_Line* const LineArray = static_cast<const IVP_Template_Line*>(Lines);
    for (int32_t I = 0; I < Count; ++I)
    {
        const int32_t P0 = LineArray[I].p[0];
        const int32_t P1 = LineArray[I].p[1];
        if (A == P0 && B == P1)
        {
            *Revert = 0;
            return I & 0xffff;
        }
        if (A == P1 && B == P0)
        {
            *Revert = 1;
            return I;
        }
    }
    return 0;
}

// FUN_0636b830 (.text:0636b830-0636bcd5).
// Template points: operator new(n * 0x10); each input point is appended when
// point_index returns 0 (a point equal to template point 0 is therefore
// appended again, as retail). Lines: one per distinct point pair of every
// plane outline, deduplicated through IVP_Hash(n_points_in * 2, 4, 0) keyed by
// the ushort pair {max, min}; each kept as a calloc(1, 4) {a, b}, copied into
// operator new(n_lines * 4) and freed. Surfaces: new IVP_Template_Surface[n]
// (n_lines = plane point count in the first pass); second pass per plane:
// templ, normal x / y / z, lines = calloc(n, 2), revert_line = operator new(n),
// point indices of the outline (calloc(n + 1, 2), closed by idx[n] = idx[0]),
// lines[k] = line_index(idx[k], idx[k + 1]) and revert_line[k] = 1 - revert.
IVP_Template_Polygon* IVP_SurfaceBuilder_Pointsoup::planes_to_template(IVP_U_Vector<IVP_U_Float_Point>* Points,
    IVP_U_Vector<IVP_SurMan_PS_Plane>* Planes)
{
    IVP_Template_Polygon* const Templ = new IVP_Template_Polygon();
    const int32_t NInput = Points->n_elems;
    Templ->n_points = 0;
    Templ->points = static_cast<IVP_U_Float_Point*>(::operator new(static_cast<size_t>(NInput) << 4));
    for (int32_t I = 0; I < NInput; ++I)
    {
        const IVP_U_Float_Point* const Point = Points->elems[I];
        if (point_index(Templ->points, Templ->n_points, Point) == 0)
        {
            IVP_U_Float_Point& Target = Templ->points[Templ->n_points];
            Target.X = Point->X;
            Target.Y = Point->Y;
            Target.Z = Point->Z;
            ++Templ->n_points;
        }
    }

    IVP_Hash* const LineHash = new IVP_Hash(Points->n_elems * 2, 4, 0);
    IVP_U_Vector<IVP_Template_Line> Lines;
    const int32_t NPlanes = Planes->n_elems;
    Templ->n_surfaces = NPlanes;
    Templ->surfaces = new IVP_Template_Surface[NPlanes];
    Templ->n_lines = 0;
    for (int32_t I = 0; I < NPlanes; ++I)
    {
        IVP_SurMan_PS_Plane* const Plane = Planes->elems[I];
        const int32_t NOutline = Plane->points.n_elems;
        Templ->surfaces[I].n_lines = NOutline;
        for (int32_t K = 0; K < NOutline; ++K)
        {
            const int32_t A = point_index(Templ->points, Templ->n_points, Plane->points.elems[K]);
            const IVP_U_Float_Point* const NextPoint =
                (K + 1 == NOutline) ? Plane->points.elems[0] : Plane->points.elems[K + 1];
            const int32_t B = point_index(Templ->points, Templ->n_points, NextPoint);
            uint16_t Key[2];
            if (static_cast<uint16_t>(B) < static_cast<uint16_t>(A))
            {
                Key[0] = static_cast<uint16_t>(A);
                Key[1] = static_cast<uint16_t>(B);
            }
            else
            {
                Key[0] = static_cast<uint16_t>(B);
                Key[1] = static_cast<uint16_t>(A);
            }
            if (LineHash->Find(Key) == 0)
            {
                LineHash->Add(Key, 1);
                IVP_Template_Line* const Line = static_cast<IVP_Template_Line*>(calloc(1, 4));
                Line->p[0] = static_cast<uint16_t>(A);
                Line->p[1] = static_cast<uint16_t>(B);
                Lines.Add(Line);
                ++Templ->n_lines;
            }
        }
    }
    delete LineHash;

    Templ->lines = static_cast<IVP_Template_Line*>(::operator new(static_cast<size_t>(Templ->n_lines) * 4));
    for (int32_t I = 0; I < Lines.n_elems; ++I)
    {
        IVP_Template_Line* const Line = Lines.elems[I];
        Templ->lines[I].p[0] = Line->p[0];
        Templ->lines[I].p[1] = Line->p[1];
        if (Line != nullptr)
        {
            free(Line);
        }
    }

    for (int32_t I = 0; I < NPlanes; ++I)
    {
        IVP_SurMan_PS_Plane* const Plane = Planes->elems[I];
        IVP_Template_Surface& Surface = Templ->surfaces[I];
        Surface.templ = Templ;
        Surface.normal[0] = Plane->hesse[0];
        Surface.normal[1] = Plane->hesse[1];
        Surface.normal[2] = Plane->hesse[2];
        Surface.lines = static_cast<uint16_t*>(calloc(static_cast<size_t>(Surface.n_lines), 2));
        Surface.revert_line = static_cast<char*>(::operator new(static_cast<size_t>(Surface.n_lines)));
        const int32_t NOutline = Surface.n_lines;
        uint16_t* const Indices = static_cast<uint16_t*>(calloc(static_cast<size_t>(NOutline + 1), 2));
        for (int32_t K = 0; K < NOutline; ++K)
        {
            Indices[K] = static_cast<uint16_t>(point_index(Templ->points, Templ->n_points, Plane->points.elems[K]));
        }
        Indices[NOutline] = Indices[0];
        for (int32_t K = 0; K < NOutline; ++K)
        {
            // Retail reuses a stale stack byte here; it is always overwritten,
            // since every outline line was registered in the first pass.
            uint8_t Revert = 0;
            Surface.lines[K] =
                static_cast<uint16_t>(line_index(Templ->lines, Templ->n_lines, Indices[K], Indices[K + 1], &Revert));
            Surface.revert_line[K] = static_cast<char>(1 - Revert);
        }
        if (Indices != nullptr)
        {
            free(Indices);
        }
    }
    return Templ;
}

// FUN_0636be20 (.text:0636be20-0636c250).
// *Retry = 0. Every qhull facet (FORALLfacets) becomes an IVP_SurMan_PS_Plane
// (normal[i] = float(facet->normal[i]) for i < hull_dim) whose points are the
// qh_facet3vertex vertices (point ids collected; Used[id]++). With
// area = get_area_size() (negated with the normal when below 0 or unordered)
// and sqrt(get_qlen_of_all_edges()) * 0.005 > area (a sliver), unless one of
// its points is already Excluded: far1 = the point farthest from point 0,
// far2 = the point farthest from far1 (each distance dx^2 + (dz^2 + dy^2) with
// dx kept unrounded, the running maximum stored as a float from -1.0f;
// *Retry = 1 on every far2 step), and the first point that is neither far1
// nor far2 gets Excluded[id]++. qh_settempfree on the vertex set.
// No retry: planes_to_template -> IVP_SurfaceBuilder_Polygon_Convex
// (0636e080) -> the ledge, the template destroyed. Planes freed either way.
IVP_Compact_Ledge* IVP_SurfaceBuilder_Pointsoup::try_to_build_convex_ledge_from_qhull_result(
    IVP_U_Vector<IVP_U_Float_Point>* Points, int32_t* Retry, char* Excluded, char* Used)
{
    *Retry = 0;
    IVP_U_Vector<IVP_SurMan_PS_Plane> Planes;
    IVP_U_Vector<void> PointIds;

    facetT* facet;
    FORALLfacets
    {
        IVP_SurMan_PS_Plane* const Plane = new IVP_SurMan_PS_Plane();
        Planes.Add(Plane);
        for (int I = 0; I < qh hull_dim; ++I)
        {
            Plane->hesse[I] = (float)(facet->normal[I]);
        }

        setT* Vertices = qh_facet3vertex(facet);
        PointIds.n_elems = 0;
        vertexT* vertex;
        vertexT** vertexp;
        FOREACHvertex_(Vertices)
        {
            const int Id = qh_pointid(vertex->point);
            PointIds.Add(reinterpret_cast<void*>(static_cast<intptr_t>(Id)));
            Plane->points.Add(Points->elems[Id]);
            ++Used[Id];
        }

        float Area = Plane->get_area_size();
        const double QLen = Plane->get_qlen_of_all_edges();
        double AreaValue = Area;
        if (!(Area >= 0.0f))
        {
            Plane->hesse[0] = Plane->hesse[0] * kMinusOne;
            Plane->hesse[1] = Plane->hesse[1] * kMinusOne;
            Plane->hesse[2] = Plane->hesse[2] * kMinusOne;
            AreaValue = AreaValue * kMinusOne;
        }

        if (std::sqrt(QLen) * kSmallAreaFactor > AreaValue)
        {
            const int32_t NIds = PointIds.n_elems;
            int32_t Far1 = 0;
            int32_t Far2 = 0;
            float MaxDist = kMinusOne;
            bool bAbort = false;
            for (int32_t K = 0; K < NIds; ++K)
            {
                const intptr_t Id = reinterpret_cast<intptr_t>(PointIds.elems[K]);
                if (Excluded[Id] != 0)
                {
                    bAbort = true;
                    break;
                }
                const IVP_U_Float_Point* const P0 = Plane->points.elems[0];
                const IVP_U_Float_Point* const Pk = Plane->points.elems[K];
                const double Dx = (double)(P0->X) - Pk->X;
                const float Dy = (float)((double)(P0->Y) - Pk->Y);
                const float Dz = (float)((double)(P0->Z) - Pk->Z);
                const double Dist = Dx * Dx + ((double)(Dz) * Dz + (double)(Dy) * Dy);
                if (Dist > MaxDist)
                {
                    MaxDist = (float)(Dist);
                    Far1 = K;
                }
            }

            if (!bAbort)
            {
                MaxDist = kMinusOne;
                for (int32_t K = 0; K < NIds; ++K)
                {
                    *Retry = 1;
                    const IVP_U_Float_Point* const Pf = Plane->points.elems[Far1];
                    const IVP_U_Float_Point* const Pk = Plane->points.elems[K];
                    const double Dx = (double)(Pf->X) - Pk->X;
                    const float Dy = (float)((double)(Pf->Y) - Pk->Y);
                    const float Dz = (float)((double)(Pf->Z) - Pk->Z);
                    const double Dist = Dx * Dx + ((double)(Dz) * Dz + (double)(Dy) * Dy);
                    if (Dist > MaxDist)
                    {
                        MaxDist = (float)(Dist);
                        Far2 = K;
                    }
                }
                for (int32_t K = 0; K < NIds; ++K)
                {
                    if (K == Far2 || K == Far1)
                    {
                        continue;
                    }
                    ++Excluded[reinterpret_cast<intptr_t>(PointIds.elems[K])];
                    break;
                }
            }
        }

        qh_settempfree(&Vertices);
    }

    IVP_Compact_Ledge* Ledge = nullptr;
    if (*Retry == 0)
    {
        IVP_Template_Polygon* const Templ = planes_to_template(Points, &Planes);
        Ledge = IVP_SurfaceBuilder_Polygon_Convex::convert_template_to_ledge(Templ);
        if (Templ != nullptr)
        {
            delete Templ;
        }
    }

    for (int32_t I = 0; I < Planes.n_elems; ++I)
    {
        IVP_SurMan_PS_Plane* const Plane = Planes.elems[I];
        if (Plane != nullptr)
        {
            delete Plane;
        }
    }
    return Ledge;
}

// FUN_0636c260 (.text:0636c260-0636c7b4).
// Points are deduplicated (stack FPointsoupPointHash(0x400)) into a reduced
// vector (malloc(n * 4), memsize n) and a double array calloc(n * 4, 8),
// 3 doubles per point. Loop: "qhull Qs Pp C-0 W1e-14 E1.0e-18" while the
// first-pass flag is set (qh_freeqhull(!qh_ALL) first when qhull ran
// before); on a failure (or once the flag is clear) the joggled
// "qhull Qs QJ%G C-0 Pp W1e-14 E1.0e-18" with the float joggle (1e-12f
// first); a joggled failure clears the flag, joggle = float((j + 1e-12f) * 1.2f).
// On success both flag arrays are zeroed and the qhull result is tried;
// a ledge ends the loop (its point +0xc values are copied from the equal
// input points through a second hash); otherwise only points used by a facet
// and not excluded are kept (coordinates and vector entries compacted); an
// unchanged count also clears the flag and grows the joggle; 3 points left
// recurse into convert_pointsoup_to_compact_ledge (single triangle), fewer
// end the loop; it repeats while joggle < 0.02f (or unordered).
// End: qh_freeqhull(!qh_ALL), qh_memfreeshort, the arrays freed.
IVP_Compact_Ledge* IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge_internal(
    IVP_U_Vector<IVP_U_Float_Point>* Points)
{
    const int32_t NInput = Points->n_elems;
    IVP_U_Vector<IVP_U_Float_Point> Reduced(
        NInput != 0 ? static_cast<IVP_U_Float_Point**>(malloc(static_cast<size_t>(NInput) * 4)) : nullptr, NInput);
    double* const Coords = static_cast<double*>(calloc(static_cast<size_t>(Points->n_elems) * 4, 8));
    {
        FPointsoupPointHash Hash(0x400);
        double* Out = Coords;
        for (int32_t I = 0; I < Points->n_elems; ++I)
        {
            IVP_U_Float_Point* const Point = Points->elems[I];
            if (Hash.FindElem(Point, IVP_PointHashIndex(Point)) == nullptr)
            {
                Hash.AddElem(Point, IVP_PointHashIndex(Point));
                Reduced.Add(Point);
                Out[0] = Point->X;
                Out[1] = Point->Y;
                Out[2] = Point->Z;
                Out += 3;
            }
        }
    }

    int32_t NPoints = Reduced.n_elems;
    char* const Excluded = static_cast<char*>(calloc(static_cast<size_t>(NPoints), 1));
    char* const Used = static_cast<char*>(calloc(static_cast<size_t>(NPoints), 1));
    bool bQhullRan = false;
    IVP_Compact_Ledge* Result = nullptr;
    int32_t bFirstPass = 1;
    float Joggle = kJoggleStart;
    char Options[0x100];

    for (;;)
    {
        sprintf(Options, "qhull Qs Pp C-0 W1e-14 E1.0e-18");
        bool bBuilt = false;
        if (bFirstPass != 0)
        {
            if (bQhullRan)
            {
                qh_freeqhull(!qh_ALL);
            }
            const int ExitCode = qh_new_qhull(3, NPoints, Coords, False, Options, nullptr, nullptr);
            bQhullRan = true;
            bBuilt = (ExitCode == 0);
        }

        if (!bBuilt)
        {
            // LAB_0636c434..0636c455 (category 1, string 063ee8a8).
            if (G_IVPMessageRouter.IsCategoryEnabled(1) != 0)
            {
                G_IVPMessageRouter.Report(1, "*** Qhull failed. Retrying with different parameters.\n");
            }
            sprintf(Options, "qhull Qs QJ%G C-0 Pp W1e-14 E1.0e-18", (double)(Joggle));
            if (bQhullRan)
            {
                qh_freeqhull(!qh_ALL);
            }
            const int ExitCode = qh_new_qhull(3, NPoints, Coords, False, Options, nullptr, nullptr);
            bQhullRan = true;
            if (ExitCode != 0)
            {
                bFirstPass = 0;
                Joggle = (float)(((double)(Joggle) + kJoggleAdd) * kJoggleScale);
                if (Joggle >= kJoggleLimit)
                {
                    break;
                }
                continue;
            }
        }

        std::memset(Used, 0, static_cast<size_t>(NPoints));
        std::memset(Excluded, 0, static_cast<size_t>(NPoints));
        int32_t Retry = 0;
        Result = try_to_build_convex_ledge_from_qhull_result(&Reduced, &Retry, Excluded, Used);
        if (Result != nullptr)
        {
            FPointsoupPointHash Hash(0x400);
            for (int32_t I = 0; I < Reduced.n_elems; ++I)
            {
                IVP_U_Float_Point* const Point = Reduced.elems[I];
                Hash.AddElem(Point, IVP_PointHashIndex(Point));
            }
            IVP_Compact_Ledge* const Ledge = Result;
            const uint32_t SizeWord = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(Ledge) + 8);
            const int32_t SizeBytes = static_cast<int32_t>((SizeWord >> 8) << 4);
            const int32_t NLedgePoints = SizeBytes / 16 - Ledge->NTriangles - 1;
            for (int32_t J = 0; J < NLedgePoints; ++J)
            {
                uint8_t* const LedgePoint = reinterpret_cast<uint8_t*>(Ledge) + Ledge->CPointOffset + J * 16;
                const uint8_t* const Found = static_cast<const uint8_t*>(Hash.FindElem(LedgePoint, IVP_PointHashIndex(LedgePoint)));
                if (Found != nullptr)
                {
                    std::memcpy(LedgePoint + 0xc, Found + 0xc, 4);
                }
            }
            break;
        }

        int32_t Kept = 0;
        for (int32_t I = 0; I < NPoints; ++I)
        {
            if (Used[I] != 0 && Excluded[I] == 0)
            {
                std::memcpy(&Coords[Kept * 3], &Coords[I * 3], 3 * sizeof(double));
                Reduced.elems[Kept] = Reduced.elems[I];
                ++Kept;
            }
        }
        Reduced.n_elems = static_cast<unsigned short>(Kept);
        if (NPoints == Kept)
        {
            bFirstPass = 0;
            Joggle = (float)(((double)(Joggle) + kJoggleAdd) * kJoggleScale);
        }
        NPoints = Kept;
        if (Kept == 3)
        {
            Result = convert_pointsoup_to_compact_ledge(&Reduced);
            break;
        }
        if (Kept < 3)
        {
            break;
        }
        if (Joggle >= kJoggleLimit)
        {
            break;
        }
    }

    qh_freeqhull(!qh_ALL);
    int CurLong;
    int TotLong;
    qh_memfreeshort(&CurLong, &TotLong);
    if (Coords != nullptr)
    {
        free(Coords);
    }
    if (Excluded != nullptr)
    {
        free(Excluded);
    }
    if (Used != nullptr)
    {
        free(Used);
    }
    // 0636c72a..0636c753 (null result, category 1, string 063ee804) and
    // 0636c756..0636c777 (category 1, string 063ee7e0).
    if (Result == nullptr)
    {
        if (G_IVPMessageRouter.IsCategoryEnabled(1) != 0)
        {
            G_IVPMessageRouter.Report(1, "*** IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_template_polygon - couldn't build convex hull! Skipping object...\n");
        }
    }
    if (G_IVPMessageRouter.IsCategoryEnabled(1) != 0)
    {
        G_IVPMessageRouter.Report(1, "*** Done with convex pointsoup.\n\n");
    }
    return Result;
}

// FUN_0636c800 (.text:0636c800-0636c99c).
// The template ledge DAT_0658b008 is built once from the triangle
// (0,0,1), (0,1,0), (0,0,-1) through 0636e0c0 ({0, 2, points}). Degenerate
// test: A = float(p1 - p0), B = p2 - p0 (unrounded), C = (float(By*Az - Bz*Ay),
// float(Bz*Ax - Bx*Az), Bx*Ay - By*Ax unrounded); (Cy^2 + Cz^2) + Cx^2 below
// 1e-6f (or unordered) -> null. Otherwise an aligned copy (FUN_06343fc0 of
// ((tmpl[+8] >> 8) << 4) bytes, 16) of the template with points 0..2
// overwritten by p0..p2 (x, y, z).
IVP_Compact_Ledge* IVP_SurfaceBuilder_Pointsoup::single_tri_to_compact_ledge(const IVP_U_Float_Point* P0,
    const IVP_U_Float_Point* P1, const IVP_U_Float_Point* P2)
{
    if (GPointsoupTriangleTemplate == nullptr)
    {
        IVP_U_Float_Point TemplatePoints[3];
        TemplatePoints[0].X = 0.0f;
        TemplatePoints[0].Y = 0.0f;
        TemplatePoints[0].Z = 1.0f;
        TemplatePoints[1].X = 0.0f;
        TemplatePoints[1].Y = 1.0f;
        TemplatePoints[1].Z = 0.0f;
        TemplatePoints[2].X = 0.0f;
        TemplatePoints[2].Y = 0.0f;
        TemplatePoints[2].Z = -1.0f;
        IVP_Template_Triangle_Points Triangle;
        Triangle.Field_0x00 = 0;
        Triangle.Field_0x04 = 2;
        Triangle.points = TemplatePoints;
        GPointsoupTriangleTemplate = IVP_SurfaceBuilder_Polygon_Convex::convert_triangle_to_ledge(&Triangle);
    }

    const float Ax = (float)((double)(P1->X) - P0->X);
    const float Ay = (float)((double)(P1->Y) - P0->Y);
    const float Az = (float)((double)(P1->Z) - P0->Z);
    const double Bx = (double)(P2->X) - P0->X;
    const double By = (double)(P2->Y) - P0->Y;
    const double Bz = (double)(P2->Z) - P0->Z;
    const float Cx = (float)(By * Az - Bz * Ay);
    const float Cy = (float)(Bz * Ax - Bx * Az);
    const double Cz = Bx * Ay - By * Ax;
    double QLen = (double)(Cy) * Cy + Cz * Cz;
    QLen += (double)(Cx) * Cx;
    if (!(QLen >= kDegenerateTriangle))
    {
        return nullptr;
    }

    const IVP_Compact_Ledge* const Template = GPointsoupTriangleTemplate;
    const uint32_t SizeWord = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(Template) + 8);
    const unsigned Size = (SizeWord >> 8) << 4;
    IVP_Compact_Ledge* const Copy = static_cast<IVP_Compact_Ledge*>(ivp_malloc_aligned(Size, 0x10)); // FUN_06343fc0
    std::memcpy(Copy, GPointsoupTriangleTemplate, Size);

    IVP_U_Float_Point* const Out = Copy->GetPointArray();
    Out[0].Z = P0->Z;
    Out[0].X = P0->X;
    Out[0].Y = P0->Y;
    Out[1].Z = P1->Z;
    Out[1].X = P1->X;
    Out[1].Y = P1->Y;
    Out[2].X = P2->X;
    Out[2].Y = P2->Y;
    Out[2].Z = P2->Z;
    return Copy;
}

// FUN_0636c9a0 (.text:0636c9a0-0636c9f2): FSTCW / OR 0x300 / FLDCW -- the
// x87 precision control is set to 64-bit and not restored; fewer than 3
// points -> null; exactly 3 -> single_tri_to_compact_ledge; else the qhull path.
IVP_Compact_Ledge* IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(IVP_U_Vector<IVP_U_Float_Point>* Points)
{
    _control87(_PC_64, _MCW_PC);
    const int32_t Count = Points->n_elems;
    if (Count < 3)
    {
        return nullptr;
    }
    if (Count == 3)
    {
        return single_tri_to_compact_ledge(Points->elems[0], Points->elems[1], Points->elems[2]);
    }
    return convert_pointsoup_to_compact_ledge_internal(Points);
}

void IVP_SurfaceBuilder_Pointsoup::Cleanup()
{
    if (GPointsoupTriangleTemplate != nullptr)
    {
        ivp_free_aligned(GPointsoupTriangleTemplate); // FUN_06343ff0
    }
    GPointsoupTriangleTemplate = nullptr;
}

namespace
{
    // Start point of edge slot K (0..2) of a triangle (dword at tri + 4 + 4K).
    const uint8_t* LedgeEdgePoint(const IVP_Compact_Ledge* Ledge, const IVP_Compact_Triangle* Triangle, int32_t K)
    {
        const uint8_t* Points = reinterpret_cast<const uint8_t*>(Ledge) + Ledge->CPointOffset;
        return Points + ((Triangle->CEdge[K].Data & 0xffffu) << 4);
    }
}

// FUN_0636ca00 (.text:0636ca00-0636ca21).
FLedgeConvexHullBuilder::FLedgeConvexHullBuilder()
    : Ledges(static_cast<IVP_Compact_Ledge**>(malloc(0x200)), 0x80) // FUN_06343f70 = malloc
{
}

// FUN_0636cf70 (.text:0636cf70-0636cf8a).
IVP_Compact_Ledge* FLedgeConvexHullBuilder::Build()
{
    BuildHull();
    if (Result != nullptr)
        MarkVirtualFeatures();
    return Result;
}

// FUN_0636ca30 (.text:0636ca30-0636cbf4). Locals: a point hash
// FUN_06356a40(0x400 buckets, key 0x10, default 0) and an empty
// IVP_U_Vector<IVP_U_Point>. Ledges are walked from the last to the first,
// triangles in order, the 3 edge slots in order; a start point whose 16 bytes
// are not in the hash yet is inserted (key and value = the point address) and
// a fresh 0x10-byte point (operator_new; x, y, z copied, w left as allocated)
// is appended. Result = FUN_0636c9a0(&points); then the points are deleted from
// the last to the first (FUN_06391d5a), the vector is released (inline-sentinel
// test) and the hash destroyed (FUN_06356a70).
void FLedgeConvexHullBuilder::BuildHull()
{
    IVP_Hash PointHash(0x400, 0x10, 0);
    IVP_U_Vector<IVP_U_Float_Point> Points;

    for (int32_t L = Ledges.Len() - 1; L >= 0; --L)
    {
        const IVP_Compact_Ledge* Ledge = Ledges.elems[L];
        const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
        for (int32_t T = 0; T < Ledge->NTriangles; ++T, ++Triangle)
        {
            for (int32_t K = 0; K < 3; ++K)
            {
                const uint8_t* Point = LedgeEdgePoint(Ledge, Triangle, K);
                if (PointHash.Find(Point) != 0)
                    continue;
                PointHash.Add(Point, reinterpret_cast<unsigned>(Point));

                const IVP_U_Float_Point* Source = reinterpret_cast<const IVP_U_Float_Point*>(Point);
                IVP_U_Float_Point* Copy = static_cast<IVP_U_Float_Point*>(::operator new(0x10));
                Copy->X = Source->X;
                Copy->Y = Source->Y;
                Copy->Z = Source->Z;
                Points.Add(Copy);
            }
        }
    }

    Result = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&Points);

    for (int32_t I = Points.Len() - 1; I >= 0; --I)
        ::operator delete(Points.elems[I]);
}

// FUN_0636cc00 (.text:0636cc00-0636cf3b). Three hashes (0x400 buckets each):
// points (key 0x10, default -1), triangles (key 0xc, default 0), edges
// (key 8, default 0).
//  1. Input ledges from the last to the first, triangles in order: each start
//     point gets an index (Find >= 0 keeps it; otherwise Insert(point, n++)).
//     The triangle key {i0, i1, i2} (FUN_0636cf40) is inserted with the
//     triangle address when absent; then for K = 0..2 the edge key
//     {iK, i((K+1)%3)} (FUN_0636cf60) likewise.
//  2. Hull ledge (Result), triangles in order: indices by Find only; an absent
//     triangle key ORs 0x80000000 into the triangle info dword; for K = 0..2
//     an absent edge key ORs 0x80000000 into edge slot K. (Two counters of
//     present / absent edges are kept by retail and never read.)
void FLedgeConvexHullBuilder::MarkVirtualFeatures()
{
    IVP_Hash PointHash(0x400, 0x10, 0xffffffffu);
    IVP_Hash TriangleHash(0x400, 0xc, 0);
    IVP_Hash EdgeHash(0x400, 8, 0);

    int32_t NextPointIndex = 0;
    for (int32_t L = Ledges.Len() - 1; L >= 0; --L)
    {
        const IVP_Compact_Ledge* Ledge = Ledges.elems[L];
        const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
        for (int32_t T = 0; T < Ledge->NTriangles; ++T, ++Triangle)
        {
            int32_t Index[3];
            for (int32_t K = 0; K < 3; ++K)
            {
                const uint8_t* Point = LedgeEdgePoint(Ledge, Triangle, K);
                int32_t Found = static_cast<int32_t>(PointHash.Find(Point));
                if (Found < 0)
                {
                    PointHash.Add(Point, static_cast<unsigned>(NextPointIndex));
                    Found = NextPointIndex;
                    ++NextPointIndex;
                }
                Index[K] = Found;
            }

            const int32_t TriangleKey[3] = { Index[0], Index[1], Index[2] };
            if (TriangleHash.Find(TriangleKey) == 0)
                TriangleHash.Add(TriangleKey, reinterpret_cast<unsigned>(Triangle));

            for (int32_t K = 0; K < 3; ++K)
            {
                const int32_t EdgeKey[2] = { Index[K], Index[(K + 1) % 3] };
                if (EdgeHash.Find(EdgeKey) == 0)
                    EdgeHash.Add(EdgeKey, reinterpret_cast<unsigned>(Triangle));
            }
        }
    }

    IVP_Compact_Ledge* Hull = Result;
    IVP_Compact_Triangle* Triangle = Hull->GetFirstTriangle();
    for (int32_t T = 0; T < Hull->NTriangles; ++T, ++Triangle)
    {
        int32_t Index[3];
        for (int32_t K = 0; K < 3; ++K)
            Index[K] = static_cast<int32_t>(PointHash.Find(LedgeEdgePoint(Hull, Triangle, K)));

        const int32_t TriangleKey[3] = { Index[0], Index[1], Index[2] };
        if (TriangleHash.Find(TriangleKey) == 0)
            Triangle->TriInfo |= 0x80000000u;

        for (int32_t K = 0; K < 3; ++K)
        {
            const int32_t EdgeKey[2] = { Index[K], Index[(K + 1) % 3] };
            if (EdgeHash.Find(EdgeKey) == 0)
                Triangle->CEdge[K].Data |= 0x80000000u;
        }
    }
}
