// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_surface_builder_polygon_convex.h"

#include <cstring>
#include <new>

#include "ivp_compact_ledge_gen.h"
#include "ivp_object_polygon_tetra.h"
#include "ivp_template_polygon.h"
#include "../ivp_utility/ivp_hash.h"

#include "../ivp_utility/ivp_malloc.h" // IVP's 16-byte aligned block allocator (FUN_06343fc0)

// FUN_0636da30 (.text:0636da30-0636dada): point_hash = new
// FPolygonPointHash(8); tetras = new IVP_Object_Polygon_Tetra(Templ), then
// tetras->template_polygon = Templ (the tetra ctor cleared it); c_ledge = 0;
// init_surface_manager_polygon().
IVP_SurfaceBuilder_Polygon_Convex::IVP_SurfaceBuilder_Polygon_Convex(IVP_Template_Polygon* Templ)
    : tetras(nullptr)
    , c_ledge(nullptr)
    , point_hash(nullptr)
{
    point_hash = new FPolygonPointHash(8);
    tetras = new IVP_Object_Polygon_Tetra(Templ);
    tetras->template_polygon = Templ;
    c_ledge = nullptr;
    init_surface_manager_polygon();
}

// FUN_0636db00 (.text:0636db00-0636de6d).
// tetras = c_ledge = 0; point_hash = new FPolygonPointHash(8); a null
// triangle returns here. The three points are deduplicated through the hash
// (probe: x / y / z of the 16-byte source point, compact_index 0; a miss
// creates an IVP_Poly_Point (operator new(0x18), compact_index = 0) and adds
// it). make_double_triangle(p0, p1, p2) gives the triangle and its back
// side, paired as pierce partners and sharing one surface (operator new(0xc),
// only +4 zeroed). The generator compiles the pair into an aligned block
// (FUN_06343fc0(size, 0x10), zero-filled); then both triangles are deleted
// (slot 0), the surface operator-deleted, and c_ledge = the block.
IVP_SurfaceBuilder_Polygon_Convex::IVP_SurfaceBuilder_Polygon_Convex(IVP_Template_Triangle_Points* Triangle)
    : tetras(nullptr)
    , c_ledge(nullptr)
    , point_hash(nullptr)
{
    point_hash = new FPolygonPointHash(8);
    if (Triangle == nullptr)
    {
        return;
    }

    IVP_U_Vector<IVP_Triangle> Triangles;
    IVP_U_Vector<IVP_Poly_Surface> Surfaces;
    IVP_Poly_Point* Points[3];
    const IVP_U_Float_Point* const Source = Triangle->points;
    for (int32_t I = 0; I < 3; ++I)
    {
        IVP_Poly_Point Probe;
        Probe.k[0] = Source[I].X;
        Probe.k[1] = Source[I].Y;
        Probe.k[2] = Source[I].Z;
        Probe.compact_index = 0;
        IVP_Poly_Point* Found =
            static_cast<IVP_Poly_Point*>(point_hash->FindElem(&Probe, IVP_PointHashIndex(&Probe)));
        Points[I] = Found;
        if (Found == nullptr)
        {
            IVP_Poly_Point* const Point = new IVP_Poly_Point;
            Point->compact_index = 0;
            Point->k[0] = Probe.k[0];
            Point->k[2] = Probe.k[2];
            Point->k[1] = Probe.k[1];
            Points[I] = Point;
            point_hash->AddElem(Point, IVP_PointHashIndex(Point));
        }
    }

    IVP_Triangle* const Front = IVP_Object_Polygon_Tetra::make_double_triangle(Points[0], Points[1], Points[2]);
    IVP_Triangle* const Back = Front->other_side;
    Front->pierce_partner = Back;
    Back->pierce_partner = Front;
    IVP_Poly_Surface* const Surface = static_cast<IVP_Poly_Surface*>(::operator new(0xc));
    Surface->tetra = nullptr;
    Front->ivp_surface = Surface;
    Back->ivp_surface = Surface;
    Triangles.Add(Front);
    Triangles.Add(Back);
    Surfaces.Add(Surface);

    void* Buffer;
    {
        IVP_Compact_Ledge_Generator Generator;
        const int32_t Size = Generator.prepare_compact_ledge(&Triangles);
        Buffer = ivp_malloc_aligned(static_cast<unsigned>(Size), 0x10); // FUN_06343fc0
        std::memset(Buffer, 0, static_cast<size_t>(Size));
        Generator.generate_compact_ledge(Buffer);
    }

    for (int32_t I = Triangles.Len() - 1; I >= 0; --I)
    {
        if (Triangles.elems[I] != nullptr)
        {
            delete Triangles.elems[I];
        }
    }
    for (int32_t I = Surfaces.Len() - 1; I >= 0; --I)
    {
        ::operator delete(Surfaces.elems[I]);
    }
    c_ledge = static_cast<IVP_Compact_Ledge*>(Buffer);
}

// FUN_0636de70 (.text:0636de70-0636decb): every stored point of the hash
// (entries SizeMm..0, element at +4 of each 8-byte entry) operator-deleted;
// the hash deleted through slot 1 (flag 1) and cleared; the tetra destroyed
// (06379b10) and operator-deleted, then cleared. c_ledge belongs to the caller.
IVP_SurfaceBuilder_Polygon_Convex::~IVP_SurfaceBuilder_Polygon_Convex()
{
    for (int32_t I = point_hash->SizeMm; I >= 0; --I)
    {
        IVP_Poly_Point* const Point =
            static_cast<IVP_Poly_Point*>(static_cast<FPolygonPointHash::FEntry*>(point_hash->Elems)[I].Elem);
        if (Point != nullptr)
        {
            delete Point;
        }
    }
    if (point_hash != nullptr)
    {
        delete point_hash;
    }
    point_hash = nullptr;
    if (tetras != nullptr)
    {
        delete tetras;
    }
    tetras = nullptr;
}

// FUN_0636ded0 (.text:0636ded0-0636e020).
// tetras->edge_hash = new IVP_Hash(0x1f4, 8, 0) for the duration of
// make_triangles, then destroyed and cleared. An error leaves tetras and
// c_ledge null (the tetra object leaks, as retail). Otherwise calc_hesse on
// every triangle, pair_pierce_triangles, the front triangles into a vector,
// the generator compiles them into FUN_06343fc0(size, 0x10) (not zeroed on
// this path) and c_ledge = that block.
void IVP_SurfaceBuilder_Polygon_Convex::init_surface_manager_polygon()
{
    tetras->edge_hash = new IVP_Hash(0x1f4, 8, 0);
    const char* const Error = tetras->make_triangles();
    IVP_Hash* const EdgeHash = tetras->edge_hash;
    if (EdgeHash != nullptr)
    {
        delete EdgeHash;
    }
    tetras->edge_hash = nullptr;
    if (Error != nullptr)
    {
        tetras = nullptr;
        c_ledge = nullptr;
        return;
    }

    for (IVP_Triangle* Triangle = tetras->triangles; Triangle != nullptr; Triangle = Triangle->next)
    {
        Triangle->calc_hesse();
    }
    tetras->pair_pierce_triangles();

    IVP_U_Vector<IVP_Triangle> Triangles;
    add_triangles_to_vector(&Triangles);
    IVP_Compact_Ledge_Generator Generator;
    const int32_t Size = Generator.prepare_compact_ledge(&Triangles);
    void* const Buffer = ivp_malloc_aligned(static_cast<unsigned>(Size), 0x10); // FUN_06343fc0
    Generator.generate_compact_ledge(Buffer);
    c_ledge = static_cast<IVP_Compact_Ledge*>(Buffer);
}

// FUN_0636e030 (.text:0636e030-0636e06f).
void IVP_SurfaceBuilder_Polygon_Convex::add_triangles_to_vector(IVP_U_Vector<IVP_Triangle>* Triangles)
{
    for (IVP_Triangle* Triangle = tetras->triangles; Triangle != nullptr; Triangle = Triangle->next)
    {
        if (Triangle->is_back_side == 0)
        {
            Triangles->Add(Triangle);
        }
    }
}

// FUN_0636e080 (.text:0636e080-0636e0b5): null -> 0; else a stack builder
// (0636da30), its c_ledge taken and cleared, the builder destroyed (0636de70).
IVP_Compact_Ledge* IVP_SurfaceBuilder_Polygon_Convex::convert_template_to_ledge(IVP_Template_Polygon* Templ)
{
    if (Templ == nullptr)
    {
        return nullptr;
    }
    IVP_SurfaceBuilder_Polygon_Convex Builder(Templ);
    IVP_Compact_Ledge* const Ledge = Builder.c_ledge;
    Builder.c_ledge = nullptr;
    return Ledge;
}

// FUN_0636e0c0 (.text:0636e0c0-0636e0f5): the same over the triangle ctor (0636db00).
IVP_Compact_Ledge* IVP_SurfaceBuilder_Polygon_Convex::convert_triangle_to_ledge(IVP_Template_Triangle_Points* Triangle)
{
    if (Triangle == nullptr)
    {
        return nullptr;
    }
    IVP_SurfaceBuilder_Polygon_Convex Builder(Triangle);
    IVP_Compact_Ledge* const Ledge = Builder.c_ledge;
    Builder.c_ledge = nullptr;
    return Ledge;
}
