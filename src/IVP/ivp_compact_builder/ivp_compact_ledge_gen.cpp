// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_compact_ledge_gen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ivp_object_polygon_tetra.h"
#include "ivp_point_hash.h"
#include "ivp_surface_builder_polygon_convex.h"
#include "../ivp_utility/ivp_hash.h"

// Raw port in src/IVP/ivp_collision/ivp_mindist_limiter.cpp: the
// IVP_Compact_Triangle constructor (16 zero bytes).
extern "C" void* FUN_0635aa20(void* param_1_00);

namespace
{
    // FUN_0635aa50 (.text:0635aa50-0635aa5d, thiscall): the IVP_Compact_Ledge
    // constructor -- four zero dwords over the 16-byte header. Its only caller
    // is generate_compact_ledge (0637a6b1).
    void IVP_Compact_Ledge_Construct(IVP_Compact_Ledge* Ledge)
    {
        uint32_t* const Words = reinterpret_cast<uint32_t*>(Ledge);
        Words[0] = 0;
        Words[1] = 0;
        Words[2] = 0;
        Words[3] = 0;
    }

    static_assert(sizeof(IVP_Tri_Edge*) == 4, "edge_hash keys are 4-byte edge pointers");
}

// FUN_0637a2e0 (.text:0637a2e0-0637a32a): both vectors {0, 0, null}, then
// REP STOSD 0xc zero dwords over the whole object.
IVP_Compact_Ledge_Generator::IVP_Compact_Ledge_Generator()
    : point_hash(nullptr)
    , n_points(0)
    , n_edges(0)
    , n_triangles(0)
    , edge_hash(nullptr)
    , Field_0x24(0)
    , compact_ledge(nullptr)
    , triangles(nullptr)
{
}

// FUN_0637a750 (.text:0637a750-0637a86f): calc_hesse on every triangle of the
// input vector; points vector released (FUN_06343fb0 unless inline); point
// hash deleted through its vtable (slot 1, flag 1); edge hash destroyed
// (06356a70) and freed; every compact triangle operator-deleted; then the
// compact triangle vector and the point vector are released (explicitly and
// again by their member destructors, as retail does).
IVP_Compact_Ledge_Generator::~IVP_Compact_Ledge_Generator()
{
    for (int32_t I = 0; I < n_triangles; ++I)
    {
        triangles->elems[I]->calc_hesse();
    }
    points.Clear();
    if (point_hash != nullptr)
    {
        delete point_hash;
    }
    point_hash = nullptr;
    if (edge_hash != nullptr)
    {
        delete edge_hash;
    }
    edge_hash = nullptr;
    for (int32_t I = 0; I < n_triangles; ++I)
    {
        ::operator delete(c_triangles.elems[I]);
    }
    c_triangles.Clear();
}

// FUN_0637a330 (.text:0637a330-0637a696).
// 1. triangles = Triangles, n_triangles = its count; hash size = 16, doubled
//    while below n_triangles * 3 / 2 (signed division) when that exceeds 16;
//    point_hash = new FPolygonPointHash(size).
// 2. Every triangle gets index = i; each edge's start point not yet in the
//    point hash gets compact_index = n_points++, is added to the hash and
//    appended to points.
// 3. n_edges = 0; edge_hash = new IVP_Hash(size * 4, 4, 0xffffffff); per
//    triangle a compact triangle (operator new(0x10) + 0635aa20): tri index
//    (12 bits), pierce index = pierce_partner->index << 12 (or the printf
//    "no valid pierce index!? Probably a backside triangle...\n"), then
//    AND 0x80ffffff; each edge's start index (16 bits) and
//    edge_hash[edge] = i * 4 + 1 + k; appended to c_triangles.
// 4. Each compact edge's opposite offset = edge_hash[edge->opposite] - (i*4+1+k)
//    in bits 16..30 (AND 0x7fff, keeping bit 31 and bits 0..15).
// Returns (n_points + n_triangles + 1) * 16.
int32_t IVP_Compact_Ledge_Generator::prepare_compact_ledge(IVP_U_Vector<IVP_Triangle>* Triangles)
{
    triangles = Triangles;
    n_triangles = Triangles->n_elems;
    int32_t HashSize = 0x10;
    const int32_t Wanted = (n_triangles * 3) / 2;
    if (Wanted > HashSize)
    {
        do
        {
            HashSize <<= 1;
        } while (HashSize < Wanted);
    }
    point_hash = new FPolygonPointHash(HashSize);

    for (int32_t I = 0; I < n_triangles; ++I)
    {
        IVP_Triangle* const Triangle = Triangles->elems[I];
        Triangle->index = I;
        IVP_Tri_Edge* Edge = &Triangle->three_edges[0];
        for (int32_t K = 3; K != 0; --K)
        {
            IVP_Poly_Point* const Point = Edge->start_point;
            if (point_hash->FindElem(Point, IVP_PointHashIndex(Point)) == nullptr)
            {
                Point->compact_index = n_points;
                ++n_points;
                point_hash->AddElem(Point, IVP_PointHashIndex(Point));
                points.Add(Point);
            }
            Edge = Edge->next;
        }
    }

    n_edges = 0;
    edge_hash = new IVP_Hash(HashSize * 4, 4, 0xffffffffu);
    for (int32_t I = 0; I < n_triangles; ++I)
    {
        IVP_Triangle* const Triangle = Triangles->elems[I];
        IVP_Compact_Triangle* const CompactTriangle =
            static_cast<IVP_Compact_Triangle*>(FUN_0635aa20(::operator new(0x10)));

        uint32_t Info = CompactTriangle->TriInfo;
        Info ^= (static_cast<uint32_t>(Triangle->index) ^ Info) & 0xfffu;
        CompactTriangle->TriInfo = Info;
        if (Triangle->pierce_partner != nullptr)
        {
            CompactTriangle->TriInfo =
                ((static_cast<uint32_t>(Triangle->pierce_partner->index) & 0xfffu) << 12) | (Info & 0xff000fffu);
        }
        else
        {
            printf("no valid pierce index!? Probably a backside triangle...\n");
        }
        CompactTriangle->TriInfo &= 0x80ffffffu;

        IVP_Tri_Edge* Edge = &Triangle->three_edges[0];
        int32_t Unit = I * 4 + 1;
        for (int32_t K = 0; K < 3; ++K)
        {
            uint32_t Data = CompactTriangle->CEdge[K].Data;
            Data ^= (static_cast<uint32_t>(Edge->start_point->compact_index) ^ Data) & 0xffffu;
            CompactTriangle->CEdge[K].Data = Data;
            IVP_Tri_Edge* const Key = Edge;
            edge_hash->Add(&Key, static_cast<unsigned>(Unit));
            ++n_edges;
            Edge = Edge->next;
            ++Unit;
        }
        c_triangles.Add(CompactTriangle);
    }

    int32_t Base = -1;
    for (int32_t I = 0; I < n_triangles; ++I, Base -= 4)
    {
        IVP_Compact_Triangle* const CompactTriangle = c_triangles.elems[I];
        IVP_Tri_Edge* Edge = &Triangles->elems[I]->three_edges[0];
        int32_t Self = Base;
        for (int32_t K = 0; K < 3; ++K)
        {
            IVP_Tri_Edge* const Key = Edge->opposite;
            const int32_t OppositeUnit = static_cast<int32_t>(edge_hash->Find(&Key));
            CompactTriangle->CEdge[K].Data = ((static_cast<uint32_t>(OppositeUnit + Self) & 0x7fffu) << 16)
                | (CompactTriangle->CEdge[K].Data & 0x8000ffffu);
            Edge = Edge->next;
            --Self;
        }
    }

    return (n_points + n_triangles + 1) << 4;
}

// FUN_0637a6a0 (.text:0637a6a0-0637a74b): compact_ledge = Buffer; header
// zeroed (0635aa50); n_triangles (+0xc word); the compact triangles copied
// (4 dwords each) from +0x10; c_point_offset = their end; each point x, y, z
// copied and +0xc = 0; dword +8 = ((size >> 4) << 8) | (old & 0xf3) | 4.
void IVP_Compact_Ledge_Generator::generate_compact_ledge(void* Buffer)
{
    IVP_Compact_Ledge* const Ledge = static_cast<IVP_Compact_Ledge*>(Buffer);
    compact_ledge = Ledge;
    IVP_Compact_Ledge_Construct(Ledge);
    Ledge->NTriangles = static_cast<int16_t>(n_triangles);

    uint8_t* const LedgeBytes = reinterpret_cast<uint8_t*>(Ledge);
    uint8_t* Cursor = LedgeBytes + 0x10;
    for (int32_t I = 0; I < n_triangles; ++I)
    {
        std::memcpy(Cursor, c_triangles.elems[I], 0x10);
        Cursor += 0x10;
    }
    Ledge->CPointOffset = static_cast<int32_t>(Cursor - LedgeBytes);

    for (int32_t I = 0; I < n_points; ++I)
    {
        float* const Out = reinterpret_cast<float*>(Cursor);
        *reinterpret_cast<uint32_t*>(Out + 3) = 0;
        const IVP_Poly_Point* const Point = points.elems[I];
        Out[0] = Point->k[0];
        Out[2] = Point->k[2];
        Out[1] = Point->k[1];
        Cursor += 0x10;
    }

    uint32_t* const SizeWord = reinterpret_cast<uint32_t*>(LedgeBytes + 8);
    const int32_t Size = static_cast<int32_t>(Cursor - LedgeBytes);
    *SizeWord = (static_cast<uint32_t>(Size >> 4) << 8) | (*SizeWord & 0xf3u) | 4u;
}
