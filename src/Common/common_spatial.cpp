// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll spatial partitions (.text:0632e830-06331a00 plus the node pool at
// .text:06332140): NewSpatial / FreeSpatial, BspPartition, LinearPartition and the process-wide
// pool every BSP node comes from (DAT_0640a728).
//
// Floating point: retail evaluates on the x87 stack with 53-bit precision control. A value that
// stays on the stack is kept as double here; a value retail stores with FSTP float is rounded to
// float at the same point. Summation orders follow each function's own instruction order.

#include "common_spatial.h"
#include "common_misc.h" // Geometry::Sphere / Geometry::Frustum

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <vector>


// DACOM.DLL!stricmp (Common IAT 0639902c), src/Common/common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // DAT_0640a748 / DAT_0640a74c: zeroed by every sphere / ray query, read nowhere.
    unsigned GSpatialQueryStatA = 0;
    unsigned GSpatialQueryStatB = 0;
    // DAT_0640a750: +1 per Add, -1 per Remove, over all partitions; read nowhere.
    int GSpatialObjectCount = 0;

    double LengthSquared(Vector const& V) // FUN_062c4780: (x*x + y*y) + z*z
    {
        return ((double)(V.x) * V.x + (double)(V.y) * V.y) + (double)(V.z) * V.z;
    }

    double Length(Vector const& V) // FUN_06347e60
    {
        return std::sqrt(LengthSquared(V));
    }

    double Dot(Vector const& A, Vector const& B) // FUN_062c47a0: (a.z*b.z + a.y*b.y) + a.x*b.x
    {
        return ((double)(A.z) * B.z + (double)(A.y) * B.y) + (double)(A.x) * B.x;
    }

    Vector Subtract(Vector const& A, Vector const& B) // FUN_06288800: out = a - b
    {
        Vector Out;
        Out.x = A.x - B.x;
        Out.y = A.y - B.y;
        Out.z = A.z - B.z;
        return Out;
    }

    Vector AddVectors(Vector const& A, Vector const& B) // FUN_062887d0: out = a + b
    {
        Vector Out;
        Out.x = A.x + B.x;
        Out.y = A.y + B.y;
        Out.z = A.z + B.z;
        return Out;
    }

    Vector Scale(Vector const& V, float S) // FUN_06288830: out = v * s
    {
        Vector Out;
        Out.x = S * V.x;
        Out.y = S * V.y;
        Out.z = S * V.z;
        return Out;
    }

    void Accumulate(Vector& Sum, Vector const& V) // FUN_062887a0: this += v (v + this)
    {
        Sum.x = V.x + Sum.x;
        Sum.y = V.y + Sum.y;
        Sum.z = V.z + Sum.z;
    }

    void ScaleInPlace(Vector& V, float S) // FUN_062e25c0: this *= s
    {
        V.x = S * V.x;
        V.y = S * V.y;
        V.z = S * V.z;
    }

    void NormalizeInPlace(Vector& V) // FUN_0632f940
    {
        double const Inv = 1.0 / Length(V);
        V.x = (float)(Inv * V.x);
        V.y = (float)(Inv * V.y);
        V.z = (float)(Inv * V.z);
    }

    void DivideByLength(Vector& V, float Len) // FUN_062dc410: this *= 1 / len
    {
        double const Inv = 1.0 / (double)(Len);
        V.x = (float)(Inv * V.x);
        V.y = (float)(Inv * V.y);
        V.z = (float)(Inv * V.z);
    }

    // FUN_06330960 (cdecl): out = in / s, through one reciprocal that x and y use rounded to
    // float (FST) and z uses at full precision.
    Vector DivideByScalar(Vector const& In, float S)
    {
        double const InvD = 1.0 / (double)(S);
        float const InvF = (float)(InvD);
        Vector Out;
        Out.x = (float)(InvF * In.x);
        Out.y = (float)(InvF * In.y);
        Out.z = (float)(InvD * In.z);
        return Out;
    }

    // FUN_0632f980: the three counts are usable unless their unsigned sum wrapped.
    bool CountsValid(unsigned const Counts[3])
    {
        unsigned const Sum = Counts[0] + Counts[1] + Counts[2];
        return Counts[0] < Sum && Counts[1] < Sum && Counts[2] < Sum;
    }

    // FUN_0632fff0: |c2 - c1| + 2 * c0, or 9999 when the counts are not valid.
    unsigned ScoreCounts(unsigned const Counts[3])
    {
        if (!CountsValid(Counts))
        {
            return 9999;
        }
        int const Diff = (int)(Counts[2] - Counts[1]);
        return (unsigned)((Diff < 0 ? -Diff : Diff) + (int)Counts[0] * 2);
    }

    // The 0x10-byte plane at the start of every BSP node.
    struct FSpatialPlane
    {
        Vector Normal;  // +0x00
        float Distance; // +0x0c

        // FUN_0632f920: ((p.z*n.z + p.y*n.y) + p.x*n.x) - d.
        double SignedDistance(Vector const& P) const
        {
            return (((double)(P.z) * Normal.z + (double)(P.y) * Normal.y) +
                    (double)(P.x) * Normal.x) - Distance;
        }

        // FUN_0632f3f0: 1 when the sphere lies beyond the plane, 2 when it lies behind it,
        // 0 when it touches it. `TEST AH,0x41 / JNZ` then `TEST AH,5 / JP`: an unordered
        // distance lands on 0.
        int Classify(Vector const& P, float Radius) const
        {
            double const S = SignedDistance(P);
            if (S > Radius)
            {
                return 1;
            }
            if (S < -(double)(Radius))
            {
                return 2;
            }
            return 0;
        }

        void Build(Vector const& Point, Vector const& Direction); // FUN_0632f8b0
        bool BuildFromEdge(FSpatialEntry const& A, FSpatialEntry const& B); // FUN_0632fee0
        void ComputeInitialSplit(FSpatialEntry const* Entries, int Count);  // FUN_0632f480
        void ComputeCentroidSplit(FSpatialEntry const* Entries, int Count); // FUN_063300e0
    };
    static_assert(sizeof(FSpatialPlane) == 0x10, "FSpatialPlane size is 0x10");

    // The side test written out in FUN_06330020: ((p.y*n.y + p.x*n.x) + n.z*p.z) - d.
    int ClassifyYXZ(FSpatialPlane const& Plane, FSpatialEntry const& Entry)
    {
        double const S = (((double)(Entry.Position.y) * Plane.Normal.y +
                           (double)(Entry.Position.x) * Plane.Normal.x) +
                          (double)(Plane.Normal.z) * Entry.Position.z) - Plane.Distance;
        if (S > Entry.Radius)
        {
            return 1;
        }
        if (S < -(double)Entry.Radius)
        {
            return 2;
        }
        return 0;
    }

    // FUN_0632f8b0: the plane through Point whose normal is Direction normalised. The
    // normalised components stay on the x87 stack for the distance, so they are used unrounded.
    void FSpatialPlane::Build(Vector const& Point, Vector const& Direction)
    {
        Normal = Direction;
        double const Inv = 1.0 / Length(Normal);
        double const X = Inv * Normal.x;
        Normal.x = (float)(X);
        double const Y = Inv * Normal.y;
        Normal.y = (float)(Y);
        double const Z = Inv * Normal.z;
        Normal.z = (float)(Z);
        Distance = (float)((Z * Point.z + Y * Point.y) + X * Point.x);
    }

    // FUN_0632fee0: the plane perpendicular to B - A through A's position pushed out along the
    // current normal by A's radius + 0.1. Fails when the two positions are within 0.001^(1/2).
    bool FSpatialPlane::BuildFromEdge(FSpatialEntry const& A, FSpatialEntry const& B)
    {
        float const Dx = B.Position.x - A.Position.x;
        float const Dy = B.Position.y - A.Position.y;
        float const Dz = B.Position.z - A.Position.z;
        double const DistSq = ((double)Dx * Dx + (double)Dz * Dz) + (double)Dy * Dy;
        if (!(DistSq > 0.001))
        {
            return false;
        }

        double const Push = (double)(A.Radius) + (double)(0.1f);
        double const Px = Push * Normal.x + A.Position.x;
        float const PushY = (float)(Push * Normal.y);
        float const PushZ = (float)(Push * Normal.z);
        double const Py = (double)(PushY) + A.Position.y;
        double const Pz = (double)(PushZ) + A.Position.z;

        Normal.x = Dx;
        Normal.y = Dy;
        Normal.z = Dz;
        float const Len = (float)(std::sqrt(((double)Dz * Dz + (double)Dy * Dy) +
                                                       (double)Dx * Dx));
        double const Inv = 1.0 / (double)(Len);
        float const Nx = (float)(Inv * Normal.x);
        Normal.x = Nx;
        float const Ny = (float)(Inv * Normal.y);
        Normal.y = Ny;
        double const NzD = Inv * Normal.z;
        Normal.z = (float)(NzD);
        Distance = (float)((NzD * Pz + (double)(Ny) * Py) + (double)(Nx) * Px);
        return true;
    }

    // FUN_06330020 / FUN_06330d90 output: the per-side counts of a candidate plane and a copy of it.
    struct FSpatialSplit
    {
        unsigned Counts[3];  // +0x00 on, beyond, behind
        FSpatialPlane Plane; // +0x0c
    };
    static_assert(sizeof(FSpatialSplit) == 0x1c, "FSpatialSplit size is 0x1c");

    // FUN_06330020 (thiscall on the split, args plane, entries, count).
    void ClassifyEntries(FSpatialSplit& Out, FSpatialPlane const& Plane, FSpatialEntry const* Entries, int Count)
    {
        Out.Plane = Plane;
        Out.Counts[2] = 0;
        Out.Counts[1] = 0;
        Out.Counts[0] = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            ++Out.Counts[ClassifyYXZ(Out.Plane, Entries[Index])];
        }
    }

    // FUN_06330d90 (thiscall on the split, args plane, vector). The vector's bounds are read
    // again on every pass.
    void ClassifyVector(FSpatialSplit& Out, FSpatialPlane const& Plane, std::vector<FSpatialEntry> const& Entries)
    {
        Out.Plane = Plane;
        Out.Counts[2] = 0;
        Out.Counts[1] = 0;
        Out.Counts[0] = 0;
        for (size_t Index = 0; Index < Entries.size(); ++Index)
        {
            ++Out.Counts[Out.Plane.Classify(Entries[Index].Position, Entries[Index].Radius)];
        }
    }

    // FUN_0632f480 (thiscall on the plane, args entries, count): start from the centroid plane
    // of the entries, then try the plane of every ordered pair (i < j) and keep the best split.
    void FSpatialPlane::ComputeInitialSplit(FSpatialEntry const* Entries, int Count)
    {
        Vector Centroid;
        Centroid.x = 0.0f;
        Centroid.y = 0.0f;
        Centroid.z = 0.0f;
        for (int Index = 0; Index < Count; ++Index)
        {
            Accumulate(Centroid, Entries[Index].Position);
        }
        ScaleInPlace(Centroid, (float)(1.0 / (double)(Count)));

        Vector Direction;
        if (Count == 2)
        {
            Vector Delta = Subtract(Entries[0].Position, Centroid);
            NormalizeInPlace(Delta);
            Direction = Delta;
        }
        else
        {
            Vector Sum;
            Sum.x = 0.0f;
            Sum.y = 0.0f;
            Sum.z = 0.0f;
            for (int Index = 0; Index < Count; ++Index)
            {
                Vector const Delta = Subtract(Entries[Index].Position, Centroid);
                double const Len = Length(Delta);
                if (Len > 0.01) // FCOM 0.01 / TEST AH,0x41 / JNZ skips not-greater
                {
                    Accumulate(Sum, Scale(Delta, (float)(1.0 / Len)));
                }
            }
            double const SumLen = Length(Sum);
            if (SumLen > 0.25)
            {
                ScaleInPlace(Sum, (float)(1.0 / SumLen));
                Direction = Sum;
            }
            else
            {
                Vector Delta = Subtract(Entries[0].Position, Centroid);
                NormalizeInPlace(Delta);
                Direction = Delta;
            }
        }
        Build(Centroid, Direction);

        FSpatialSplit Best;
        Best.Plane = *this;
        Best.Counts[2] = 0;
        Best.Counts[1] = 0;
        Best.Counts[0] = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            ++Best.Counts[Best.Plane.Classify(Entries[Index].Position, Entries[Index].Radius)];
        }
        unsigned BestScore = ScoreCounts(Best.Counts);
        if (BestScore == 0)
        {
            return;
        }

        for (int First = 1; First - 1 < Count; ++First)
        {
            FSpatialEntry const& A = Entries[First - 1];
            for (int Second = First; Second < Count; ++Second)
            {
                Vector const Delta = Subtract(Entries[Second].Position, A.Position);
                double const DistSq = ((double)Delta.y * Delta.y + (double)Delta.z * Delta.z) +
                                      (double)Delta.x * Delta.x;
                if (!(DistSq > 0.001))
                {
                    continue;
                }
                float const Push = (float)((double)(A.Radius) + (double)(0.1f));
                Vector const Point = AddVectors(A.Position, Scale(Normal, Push));
                Build(Point, Delta);

                FSpatialSplit Candidate;
                Candidate.Plane = *this;
                Candidate.Counts[2] = 0;
                Candidate.Counts[1] = 0;
                Candidate.Counts[0] = 0;
                for (int Index = 0; Index < Count; ++Index)
                {
                    ++Candidate.Counts[Candidate.Plane.Classify(Entries[Index].Position, Entries[Index].Radius)];
                }
                unsigned const Score = ScoreCounts(Candidate.Counts);
                if (Score < BestScore)
                {
                    Best = Candidate;
                    BestScore = Score;
                    if (Score == 0)
                    {
                        break; // leaves this row only; the outer loop runs on (.text:0632f862)
                    }
                }
            }
        }
        *this = Best.Plane;
    }

    // FUN_063300e0 (thiscall on the plane, args entries, count): the plane through the centroid
    // whose normal is the mean direction from the centroid to the entries.
    void FSpatialPlane::ComputeCentroidSplit(FSpatialEntry const* Entries, int Count)
    {
        // The three sums stay on the x87 stack for the whole loop.
        double SumX = 0.0;
        double SumY = 0.0;
        double SumZ = 0.0;
        for (int Index = 0; Index < Count; ++Index)
        {
            SumX += Entries[Index].Position.x;
            SumY += Entries[Index].Position.y;
            SumZ += Entries[Index].Position.z;
        }
        float const SumZF = (float)(SumZ);
        float const Inv = (float)(1.0 / (double)(Count));
        Vector Centroid;
        Centroid.x = (float)(SumX * Inv);
        Centroid.y = (float)(SumY * Inv);
        Centroid.z = (float)((double)(SumZF) * Inv);

        Vector Sum;
        double Len = 0.0;
        bool bUseSum = false;
        if (Count != 2)
        {
            Sum.x = 0.0f;
            Sum.y = 0.0f;
            Sum.z = 0.0f;
            for (int Index = 0; Index < Count; ++Index)
            {
                float const Dz = Entries[Index].Position.z - Centroid.z;
                float const Dy = Entries[Index].Position.y - Centroid.y;
                float const Dx = Entries[Index].Position.x - Centroid.x;
                double const DLen = std::sqrt(((double)Dz * Dz + (double)Dy * Dy) +
                                              (double)Dx * Dx);
                if (DLen > 0.01)
                {
                    double const DInv = 1.0 / DLen;
                    float const Tx = (float)(Dx * DInv);
                    float const Ty = (float)(Dy * DInv);
                    float const Tz = (float)(Dz * DInv);
                    Sum.x = Tx + Sum.x;
                    Sum.y = Ty + Sum.y;
                    Sum.z = Tz + Sum.z;
                }
            }
            Len = std::sqrt(((double)(Sum.z) * Sum.z + (double)(Sum.y) * Sum.y) +
                            (double)(Sum.x) * Sum.x);
            bUseSum = Len > 0.25; // TEST AH,0x41 / JZ: strictly greater
        }
        if (!bUseSum)
        {
            // Two entries, or no usable mean direction: towards the first entry.
            Sum.z = Entries[0].Position.z - Centroid.z;
            Sum.y = Entries[0].Position.y - Centroid.y;
            Sum.x = Entries[0].Position.x - Centroid.x;
            Len = std::sqrt(((double)(Sum.z) * Sum.z + (double)(Sum.y) * Sum.y) +
                            (double)(Sum.x) * Sum.x);
        }
        double const Inv2 = 1.0 / Len;
        Normal.x = (float)(Sum.x * Inv2);
        Normal.y = (float)(Sum.y * Inv2);
        Normal.z = (float)(Sum.z * Inv2);
        NormalizeInPlace(Normal);
        Distance = (float)(Dot(Centroid, Normal));
    }

    // The 0xbc-byte BSP node: a split plane, three children (0 = touching, 1 = beyond,
    // 2 = behind) and an 8-entry leaf bucket. A node is a split node while its normal has
    // unit length (|1 - |n|^2| < 0.05); a fresh node is all zero, so it starts as a leaf.
    struct FSpatialNode
    {
        FSpatialPlane Plane;         // +0x00
        FSpatialNode* Children[3];   // +0x10
        FSpatialEntry Slots[8];      // +0x1c (Slots[7].Object at +0xa8 is the "full" test)

        bool IsSplit() const
        {
            // FSUBR 1.0 / FABS / FCOMP 0.05 / TEST AH,5: JP (not below, or unordered) = leaf.
            return std::fabs(1.0 - LengthSquared(Plane.Normal)) < 0.05;
        }

        // The used slots: counted up to the first empty one.
        unsigned CountUsed() const
        {
            unsigned Used = 0;
            while (Used < 8 && Slots[Used].Object != nullptr)
            {
                ++Used;
            }
            return Used;
        }

        // FUN_0632f440: copy the entry into the first empty slot; false when all eight are used.
        bool AddEntry(FSpatialEntry const& Entry)
        {
            unsigned const Used = CountUsed();
            if (Used >= 8)
            {
                return false;
            }
            Slots[Used] = Entry;
            return true;
        }

        // FUN_0632f140: the node holding Object, searching this node's used slots and then the
        // three children depth first.
        FSpatialNode* FindObject(void* Object)
        {
            unsigned const Used = CountUsed();
            for (unsigned Index = 0; Index < Used; ++Index)
            {
                if (Slots[Index].Object == Object)
                {
                    return this;
                }
            }
            for (int Child = 0; Child < 3; ++Child)
            {
                if (Children[Child] != nullptr)
                {
                    FSpatialNode* const Found = Children[Child]->FindObject(Object);
                    if (Found != nullptr)
                    {
                        return Found;
                    }
                }
            }
            return nullptr;
        }
    };
    static_assert(sizeof(FSpatialNode) == 0xbc, "FSpatialNode size is 0xbc");

    // DAT_0658e740 (guard DAT_0658e738): the zeroed node every new node is copied from, built on
    // first use (FUN_063300c0 zeroes 0x2f dwords; the AddObject / AddArray copies of the guard
    // also run the no-op entry constructor over the 8 slots, which changes nothing).
    FSpatialNode const& SpatialNodeTemplate()
    {
        static FSpatialNode const Template = []()
        {
            FSpatialNode Node;
            memset(&Node, 0, sizeof(Node));
            return Node;
        }();
        return Template;
    }

    // DAT_0640a728: the one list every BSP node of every partition lives in. Nodes are carved
    // from 200-node malloc blocks (FUN_06332140) and go back onto a free chain when erased; the
    // blocks are released only when the list is destroyed at exit.
    class FSpatialNodeList
    {
    public:
        FSpatialNodeList();  // FUN_0632e850 (static initializer LAB_0632e830)
        ~FSpatialNodeList(); // LAB_0632e950 (atexit) / FUN_06331950 (same body, ctor unwind)

        // FUN_0632f9b0 and the inlined insert(end(), template) at the other allocation sites:
        // append a copy of the template and return its value.
        FSpatialNode* PushBack();

        // The search-and-erase inlined in LAB_0632eed0 / FUN_0632efa0 / FUN_06331890: unlink
        // the list node whose value is Node, if any, and chain it onto the free nodes.
        void Erase(FSpatialNode* Node);

    private:
        struct FListNode
        {
            FListNode* Next;    // +0x00 (also the free-chain link)
            FListNode* Prev;    // +0x04
            FSpatialNode Value; // +0x08
        };
        static_assert(sizeof(FListNode) == 0xc4, "FListNode size is 0xc4");

        // The payload of the +0x04 list: one malloc block (FUN_063323b0 frees it).
        struct FBlock
        {
            FListNode* Memory; // +0x00 200 nodes
            unsigned Bytes; // +0x04
        };

        FListNode* BuyNode(); // FUN_06332140

        std::list<FBlock> Blocks; // +0x04 / +0x08
        FListNode* FreeNodes;     // +0x0c
        unsigned LiveNodes;       // +0x10 nodes handed out by BuyNode, the head included
        FListNode* Head;          // +0x14
        unsigned Size;            // +0x18
    };

    FSpatialNodeList::FSpatialNodeList()
        : FreeNodes(nullptr), LiveNodes(0), Head(nullptr), Size(0)
    {
        Head = BuyNode();
        Head->Next = Head;
        Head->Prev = Head;
    }

    FSpatialNodeList::~FSpatialNodeList()
    {
        // Every node, then the head, goes onto the free chain; then the blocks are freed.
        FListNode* Node = Head->Next;
        while (Node != Head)
        {
            FListNode* const Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            Node->Next = FreeNodes;
            FreeNodes = Node;
            --LiveNodes;
            --Size;
            Node = Next;
        }
        Head->Next = FreeNodes;
        FreeNodes = Head;
        --LiveNodes;
        Head = nullptr;
        Size = 0;
        for (std::list<FBlock>::iterator It = Blocks.begin(); It != Blocks.end(); ++It)
        {
            if (It->Memory != nullptr)
            {
                free(It->Memory);
                It->Memory = nullptr;
                It->Bytes = 0;
            }
        }
        Blocks.clear();
    }

    FSpatialNodeList::FListNode* FSpatialNodeList::BuyNode()
    {
        if (FreeNodes == nullptr)
        {
            FBlock Block;
            Block.Memory = nullptr;
            Block.Bytes = 0;
            Blocks.push_back(Block);
            FBlock& Added = Blocks.back();
            unsigned const Bytes = (unsigned)sizeof(FListNode) * 200;
            Added.Memory = (FListNode*)malloc(Bytes);
            Added.Bytes = Bytes;
            for (unsigned Index = 0; Index < 200; ++Index)
            {
                FListNode* const Node = &Added.Memory[Index];
                Node->Next = FreeNodes;
                FreeNodes = Node;
            }
        }
        ++LiveNodes;
        FListNode* const Node = FreeNodes;
        if (Node != nullptr)
        {
            FreeNodes = Node->Next;
        }
        return Node;
    }

    FSpatialNode* FSpatialNodeList::PushBack()
    {
        FSpatialNode const& Template = SpatialNodeTemplate();
        FListNode* const Node = BuyNode();
        Node->Next = Head;
        Node->Prev = Head->Prev;
        Head->Prev = Node;
        Node->Prev->Next = Node;
        memcpy(&Node->Value, &Template, sizeof(FSpatialNode));
        ++Size;
        return &Head->Prev->Value;
    }

    void FSpatialNodeList::Erase(FSpatialNode* Value)
    {
        for (FListNode* Node = Head->Next; Node != Head; Node = Node->Next)
        {
            if (&Node->Value == Value)
            {
                Node->Prev->Next = Node->Next;
                Node->Next->Prev = Node->Prev;
                Node->Next = FreeNodes;
                FreeNodes = Node;
                --LiveNodes;
                --Size;
                return;
            }
        }
    }

    FSpatialNodeList GSpatialNodes;
}

// ============================================================================
// LinearPartition (RTTI .?AULinearPartition@@, vftable .rdata:063a48ec, 0x14 bytes):
// every entry in one array, every query a full scan.
// ============================================================================
struct LinearPartition : public ISpatialPartition
{
    // +0x04 allocator byte, +0x08 begin / +0x0c end / +0x10 capacity.
    std::vector<FSpatialEntry> Entries;

    // FUN_0632ee10 / body FUN_0632ee30: the array is released.
    virtual ~LinearPartition() override {}

    // CShip::get_status-shaped `XOR EAX,EAX; RET` at 06325390.
    virtual int Slot04() override { return 0; }

    // LAB_0632ea80: erase(begin, end). The object count is not touched.
    virtual void Clear() override
    {
        Entries.clear();
    }

    // LAB_0632eae0: only the global object count drops; the entry stays in the array.
    virtual void Remove(void*) override
    {
        --GSpatialObjectCount;
    }

    // LAB_0632eaf0: insert(end(), 1, *Entry), then count it.
    virtual void Add(FSpatialEntry const* Entry) override
    {
        Entries.push_back(*Entry);
        ++GSpatialObjectCount;
    }

    // FUN_0632ed10: Add (vtbl+0x10) for each entry.
    virtual void AddArray(FSpatialEntry const* Array, unsigned Count) override
    {
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            Add(&Array[Index]);
        }
    }

    // CShip::get_status (`XOR EAX,EAX; RET 8`).
    virtual int QueryFrustum(Geometry::Frustum const*, FSpatialQueryResult*) override { return 0; }

    // LAB_0632ed50: every entry whose sphere overlaps the query sphere (strictly), in array
    // order. The differences stay unrounded on the x87 stack.
    virtual int QuerySphere(Geometry::Sphere const* Sphere, FSpatialQueryResult* Result) override
    {
        GSpatialQueryStatB = 0;
        GSpatialQueryStatA = 0;
        for (size_t Index = 0; Index < Entries.size(); ++Index)
        {
            FSpatialEntry const& Entry = Entries[Index];
            double const Dx = (double)(Entry.Position.x) - Sphere->Center.x;
            double const Dy = (double)(Entry.Position.y) - Sphere->Center.y;
            double const Dz = (double)(Entry.Position.z) - Sphere->Center.z;
            double const Reach = (double)Entry.Radius + Sphere->Radius;
            double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
            if (Reach * Reach > DistSq && Entry.Object != nullptr && Result->Count < Result->Capacity)
            {
                Result->Items[Result->Count] = Entry.Object;
                ++Result->Count;
            }
        }
        return (int)Result->Count;
    }

    // CShip::get_status (`XOR EAX,EAX; RET 8`).
    virtual int QueryRay(FSpatialRay const*, FSpatialQueryResult*) override { return 0; }

    // FUN_0632edf0: zero six dwords.
    virtual void Slot24(unsigned long Out[6]) override
    {
        memset(Out, 0, 6 * sizeof(unsigned long));
    }
};

// ============================================================================
// BspPartition (RTTI .?AUBspPartition@@, vftable .rdata:063a4920, 0x0c bytes).
// ============================================================================
struct BspPartition : public ISpatialPartition
{
    FSpatialNode* Root; // +0x04
    int EntryCount;     // +0x08 entries stored in the leaves

    BspPartition() : Root(nullptr), EntryCount(0) {}

    // FUN_06331870 / body FUN_06331890: the same teardown as Clear.
    virtual ~BspPartition() override
    {
        Release();
    }

    virtual int Slot04() override { return 0; } // 06325390

    // LAB_0632eed0.
    virtual void Clear() override
    {
        Release();
    }

    virtual void Remove(void* Object) override;                                         // LAB_0632f060
    virtual void Add(FSpatialEntry const* Entry) override;                              // LAB_0632f1b0
    virtual void AddArray(FSpatialEntry const* Entries, unsigned Count) override;       // FUN_06330340
    virtual int QueryFrustum(Geometry::Frustum const* Frustum, FSpatialQueryResult* Result) override; // LAB_063314c0
    virtual int QuerySphere(Geometry::Sphere const* Sphere, FSpatialQueryResult* Result) override;    // LAB_063315b0
    virtual int QueryRay(FSpatialRay const* Ray, FSpatialQueryResult* Result) override; // LAB_06330e70

    // FUN_0632edf0 (shared with LinearPartition).
    virtual void Slot24(unsigned long Out[6]) override
    {
        memset(Out, 0, 6 * sizeof(unsigned long));
    }

private:
    void Release();                                                                       // LAB_0632eed0 / FUN_06331890 body
    void FreeNode(FSpatialNode* Node);                                                    // FUN_0632efa0
    void InsertWalk(FSpatialNode* Node, FSpatialEntry const* Entry);                      // FUN_0632fa30
    void InsertSplit(FSpatialNode* Node, FSpatialEntry const* Entry);                     // FUN_0632fbe0
    FSpatialPlane SplitEntries(std::vector<FSpatialEntry> const& Entries, std::vector<FSpatialEntry>& Touching,
                               std::vector<FSpatialEntry>& Beyond, std::vector<FSpatialEntry>& Behind); // FUN_06330630
    void Build(FSpatialNode* Node, std::vector<FSpatialEntry> const& Entries);           // FUN_06330990
    void QueryRayNode(FSpatialNode* Node, FSpatialRay const* Ray, FSpatialQueryResult* Result);      // FUN_063311e0
    void QuerySphereNode(FSpatialNode* Node, Geometry::Sphere const* Sphere, FSpatialQueryResult* Result); // FUN_06331730
};

namespace
{
    // FUN_063311c0 (thiscall on the result): append Object when set and there is room.
    void AppendResult(FSpatialQueryResult* Result, void* Object)
    {
        if (Object != nullptr && Result->Count < Result->Capacity)
        {
            Result->Items[Result->Count] = Object;
            ++Result->Count;
        }
    }

    // FUN_063310c0 (cdecl): where the ray crosses the plane. *OutSide is the side the origin is
    // on (1 beyond, 2 otherwise) and is written before any failure.
    bool IntersectRayPlane(FSpatialRay const* Ray, FSpatialPlane const* Plane, Vector* OutHit, int* OutSide)
    {
        double const S = (((double)(Ray->Origin.z) * Plane->Normal.z +
                           (double)(Ray->Origin.y) * Plane->Normal.y) +
                          (double)(Ray->Origin.x) * Plane->Normal.x) - Plane->Distance;
        double const Dn = ((double)(Ray->Direction.z) * Plane->Normal.z +
                           (double)(Ray->Direction.y) * Plane->Normal.y) +
                          (double)(Plane->Normal.x) * Ray->Direction.x;
        double Num;
        double Den;
        if (S > 0.0)
        {
            *OutSide = 1;
            Num = S;
            Den = -Dn;
        }
        else
        {
            *OutSide = 2;
            Num = -S;
            Den = Dn;
        }
        if (Den < 0.001) // FCOM 0.001 / TEST AH,5 / JP continues on not-below
        {
            return false;
        }
        float const T = (float)(Num / Den);
        if (T < 0.0f)
        {
            return false;
        }
        if (T > Ray->Length)
        {
            return false;
        }
        OutHit->x = (float)((double)T * Ray->Direction.x + Ray->Origin.x);
        OutHit->y = (float)((double)T * Ray->Direction.y + Ray->Origin.y);
        OutHit->z = (float)((double)T * Ray->Direction.z + Ray->Origin.z);
        return true;
    }

    // The leaf test of both ray walks: the entry's sphere contains the origin, or it lies
    // ahead of the origin and the ray passes closer than its radius. Dist is the float that
    // retail stores and reuses; DistSphere is the value it compares against the radius first.
    bool RayHitsEntry(FSpatialRay const* Ray, FSpatialEntry const& Entry, Vector Delta, double DistExact)
    {
        float const Dist = (float)(DistExact);
        if (DistExact < Entry.Radius) // FCOMP radius / TEST AH,5 / JNP: below
        {
            return true;
        }
        DivideByLength(Delta, Dist);
        double const Proj = Dot(Ray->Direction, Delta);
        if (Proj < 0.0)
        {
            return false;
        }
        double const Along = Proj * Dist;
        double const PerpSq = (double)(Dist) * Dist - Along * Along;
        double const RadiusSq = (double)Entry.Radius * Entry.Radius;
        return RadiusSq > PerpSq;
    }
}

void BspPartition::Release()
{
    if (Root == nullptr)
    {
        return;
    }
    if (Root->IsSplit())
    {
        for (int Child = 0; Child < 3; ++Child)
        {
            FreeNode(Root->Children[Child]);
        }
    }
    GSpatialNodes.Erase(Root);
    Root = nullptr;
    EntryCount = 0;
}

void BspPartition::FreeNode(FSpatialNode* Node)
{
    if (Node == nullptr)
    {
        return;
    }
    if (Node->IsSplit())
    {
        for (int Child = 0; Child < 3; ++Child)
        {
            FreeNode(Node->Children[Child]);
        }
    }
    GSpatialNodes.Erase(Node);
}

void BspPartition::Remove(void* Object)
{
    // Retail reads the root's slots without testing the root (.text:0632f062-0632f070): on an
    // empty partition that is a null dereference, reproduced here.
    FSpatialNode* Node = nullptr;
    unsigned const RootUsed = Root->CountUsed();
    for (unsigned Index = 0; Index < RootUsed; ++Index)
    {
        if (Root->Slots[Index].Object == Object)
        {
            Node = Root;
            break;
        }
    }
    if (Node == nullptr)
    {
        for (int Child = 0; Child < 3 && Node == nullptr; ++Child)
        {
            if (Root->Children[Child] != nullptr)
            {
                Node = Root->Children[Child]->FindObject(Object);
            }
        }
        if (Node == nullptr)
        {
            return;
        }
    }

    // The last used slot moves into the hole and is cleared. EntryCount is not touched.
    unsigned const Used = Node->CountUsed();
    for (unsigned Index = 0; Index < Used; ++Index)
    {
        if (Node->Slots[Index].Object == Object)
        {
            Node->Slots[Index] = Node->Slots[Used - 1];
            Node->Slots[Used - 1].Object = nullptr;
            break;
        }
    }
    --GSpatialObjectCount;
}

void BspPartition::Add(FSpatialEntry const* Entry)
{
    FSpatialNode* Node = Root;
    if (Node != nullptr)
    {
        if (Node->IsSplit())
        {
            InsertWalk(Node, Entry);
        }
        else if (Node->Slots[7].Object == nullptr)
        {
            Node->AddEntry(*Entry);
            ++EntryCount;
        }
        else
        {
            // A full root leaf: split it and push its eight entries down with InsertSplit.
            Node->Plane.ComputeInitialSplit(Node->Slots, 8);
            for (int Index = 0; Index < 8; ++Index)
            {
                FSpatialEntry& Slot = Node->Slots[Index];
                int const Side = Node->Plane.Classify(Slot.Position, Slot.Radius);
                FSpatialNode* Child = Node->Children[Side];
                if (Child != nullptr)
                {
                    InsertSplit(Child, &Slot);
                }
                else
                {
                    Child = GSpatialNodes.PushBack();
                    Node->Children[Side] = Child;
                    Child->AddEntry(Slot);
                    ++EntryCount;
                }
                Slot.Object = nullptr;
                --EntryCount;
            }
            InsertWalk(Node, Entry);
        }
        ++GSpatialObjectCount;
        return;
    }

    // .text:0632f275: the first root is a split node on the plane x = 0 (normal (1,0,0)
    // normalised in place, distance (z + y + x) * 0).
    Node = GSpatialNodes.PushBack();
    Root = Node;
    Node->Plane.Normal.x = 1.0f;
    Node->Plane.Normal.y = 0.0f;
    Node->Plane.Normal.z = 0.0f;
    {
        double const Inv = 1.0 / Length(Node->Plane.Normal);
        double const X = Inv * Node->Plane.Normal.x;
        Node->Plane.Normal.x = (float)(X);
        double const Y = Inv * Node->Plane.Normal.y;
        Node->Plane.Normal.y = (float)(Y);
        double const Z = Inv * Node->Plane.Normal.z;
        Node->Plane.Normal.z = (float)(Z);
        Node->Plane.Distance = (float)(((Z + Y) + X) * (double)(0.0f));
    }
    if (Node->IsSplit())
    {
        InsertWalk(Node, Entry);
    }
    else if (Node->Slots[7].Object == nullptr)
    {
        Node->AddEntry(*Entry);
        ++EntryCount;
    }
    else
    {
        Node->Plane.ComputeInitialSplit(Node->Slots, 8);
        for (int Index = 0; Index < 8; ++Index)
        {
            InsertWalk(Node, &Node->Slots[Index]);
            Node->Slots[Index].Object = nullptr;
            --EntryCount;
        }
        InsertWalk(Node, Entry);
    }
    ++GSpatialObjectCount;
}

void BspPartition::InsertWalk(FSpatialNode* Node, FSpatialEntry const* Entry)
{
    for (;;)
    {
        // Written out in place: ((n.y*p.y + n.z*p.z) + p.x*n.x) - d, the same value as Classify.
        int const Side = Node->Plane.Classify(Entry->Position, Entry->Radius);
        FSpatialNode* Child = Node->Children[Side];
        if (Child == nullptr)
        {
            Child = GSpatialNodes.PushBack();
            Node->Children[Side] = Child;
            Child->AddEntry(*Entry);
            ++EntryCount;
            return;
        }
        if (!Child->IsSplit())
        {
            if (Child->Slots[7].Object == nullptr)
            {
                Child->AddEntry(*Entry);
                ++EntryCount;
                return;
            }
            Child->Plane.ComputeInitialSplit(Child->Slots, 8);
            for (int Index = 0; Index < 8; ++Index)
            {
                InsertWalk(Child, &Child->Slots[Index]);
                Child->Slots[Index].Object = nullptr;
                --EntryCount;
            }
        }
        Node = Child;
    }
}

void BspPartition::InsertSplit(FSpatialNode* Node, FSpatialEntry const* Entry)
{
    for (;;)
    {
        if (!Node->IsSplit())
        {
            if (Node->Slots[7].Object == nullptr)
            {
                Node->AddEntry(*Entry);
                ++EntryCount;
                return;
            }

            // A full leaf: centroid plane first, then every edge plane over pairs (i < j).
            Node->Plane.ComputeCentroidSplit(Node->Slots, 8);
            FSpatialSplit Best;
            ClassifyEntries(Best, Node->Plane, Node->Slots, 8);
            unsigned BestScore = ScoreCounts(Best.Counts);
            if (BestScore != 0)
            {
                for (int First = 1; First - 1 < 8; ++First)
                {
                    for (int Second = First; Second < 8; ++Second)
                    {
                        if (!Node->Plane.BuildFromEdge(Node->Slots[First - 1], Node->Slots[Second]))
                        {
                            continue;
                        }
                        FSpatialSplit Candidate;
                        ClassifyEntries(Candidate, Node->Plane, Node->Slots, 8);
                        unsigned const Score = ScoreCounts(Candidate.Counts);
                        if (Score < BestScore)
                        {
                            Best = Candidate;
                            BestScore = Score;
                            if (Score == 0)
                            {
                                break; // leaves this row only (.text:0632fce1 -> 0632fcf4)
                            }
                        }
                    }
                }
                Node->Plane = Best.Plane;
            }

            for (int Index = 0; Index < 8; ++Index)
            {
                FSpatialEntry& Slot = Node->Slots[Index];
                int const Side = Node->Plane.Classify(Slot.Position, Slot.Radius);
                FSpatialNode* Child = Node->Children[Side];
                if (Child != nullptr)
                {
                    InsertSplit(Child, &Slot);
                }
                else
                {
                    Child = GSpatialNodes.PushBack();
                    Node->Children[Side] = Child;
                    Child->AddEntry(Slot);
                    ++EntryCount;
                }
                Slot.Object = nullptr;
                --EntryCount;
            }
        }

        int const Side = Node->Plane.Classify(Entry->Position, Entry->Radius);
        FSpatialNode* Child = Node->Children[Side];
        if (Child != nullptr)
        {
            Node = Child;
            continue;
        }
        Child = GSpatialNodes.PushBack();
        Node->Children[Side] = Child;
        Child->AddEntry(*Entry);
        ++EntryCount;
        return;
    }
}

FSpatialPlane BspPartition::SplitEntries(std::vector<FSpatialEntry> const& Entries, std::vector<FSpatialEntry>& Touching,
                                         std::vector<FSpatialEntry>& Beyond, std::vector<FSpatialEntry>& Behind)
{
    int const Count = (int)Entries.size();
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    for (int Index = 0; Index < Count; ++Index)
    {
        Accumulate(Sum, Entries[Index].Position);
    }
    Vector const Centroid = DivideByScalar(Sum, (float)(Count));

    // Retail leaves the best plane uninitialised until a candidate wins; the port starts it at
    // zero (only reachable when every entry sits on the centroid).
    int BestScore = 9999;
    FSpatialPlane Best;
    memset(&Best, 0, sizeof(Best));
    for (int Index = 0; Index < Count; ++Index)
    {
        Vector const Delta = Subtract(Entries[Index].Position, Centroid);
        double const DistSq = ((double)Delta.y * Delta.y + (double)Delta.z * Delta.z) +
                              (double)Delta.x * Delta.x;
        if (DistSq < 0.05) // FCOMP 0.05 / TEST AH,5 / JNP: below
        {
            continue;
        }
        FSpatialPlane Candidate;
        Candidate.Normal = Delta;
        double const Inv = 1.0 / Length(Candidate.Normal);
        Candidate.Normal.x = (float)(Candidate.Normal.x * Inv);
        Candidate.Normal.y = (float)(Candidate.Normal.y * Inv);
        Candidate.Normal.z = (float)(Candidate.Normal.z * Inv);
        Candidate.Distance = (float)(Dot(Candidate.Normal, Centroid));

        unsigned Counts[3] = { 0, 0, 0 };
        for (size_t Other = 0; Other < Entries.size(); ++Other)
        {
            ++Counts[Candidate.Classify(Entries[Other].Position, Entries[Other].Radius)];
        }
        int const Score = (int)ScoreCounts(Counts);
        if (Score < BestScore) // signed (JGE)
        {
            BestScore = Score;
            Best = Candidate;
            if (Score == 0)
            {
                break;
            }
        }
    }

    for (int Index = 0; Index < Count; ++Index)
    {
        FSpatialEntry const& Entry = Entries[Index];
        int const Side = Best.Classify(Entry.Position, Entry.Radius);
        if (Side == 1)
        {
            Beyond.push_back(Entry);
        }
        else if (Side == 2)
        {
            Behind.push_back(Entry);
        }
        else
        {
            Touching.push_back(Entry);
        }
    }
    return Best;
}

void BspPartition::AddArray(FSpatialEntry const* Entries, unsigned Count)
{
    std::vector<FSpatialEntry> All;
    if (Count != 0)
    {
        All.reserve((int)Count >= 0 ? Count : 0);
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            All.push_back(Entries[Index]);
        }
    }

    // Only an empty partition is built; entries offered to a populated one are dropped.
    if (Root != nullptr)
    {
        return;
    }
    Root = GSpatialNodes.PushBack();
    if (All.size() <= 8)
    {
        for (size_t Index = 0; Index < All.size(); ++Index)
        {
            Root->AddEntry(All[Index]);
            ++EntryCount;
        }
        return;
    }

    std::vector<FSpatialEntry> Touching;
    std::vector<FSpatialEntry> Beyond;
    std::vector<FSpatialEntry> Behind;
    Root->Plane = SplitEntries(All, Touching, Beyond, Behind);
    for (size_t Index = 0; Index < Touching.size(); ++Index)
    {
        InsertWalk(Root, &Touching[Index]);
    }
    if (!Beyond.empty())
    {
        FSpatialNode* const Child = GSpatialNodes.PushBack();
        Root->Children[1] = Child;
        Build(Child, Beyond);
    }
    if (!Behind.empty())
    {
        FSpatialNode* const Child = GSpatialNodes.PushBack();
        Root->Children[2] = Child;
        Build(Child, Behind);
    }
}

void BspPartition::Build(FSpatialNode* Node, std::vector<FSpatialEntry> const& Entries)
{
    if (Entries.size() <= 8)
    {
        for (size_t Index = 0; Index < Entries.size(); ++Index)
        {
            Node->AddEntry(Entries[Index]);
            ++EntryCount;
        }
        return;
    }

    std::vector<FSpatialEntry> Touching;
    std::vector<FSpatialEntry> Beyond;
    std::vector<FSpatialEntry> Behind;

    int const Count = (int)Entries.size();
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    for (int Index = 0; Index < Count; ++Index)
    {
        Accumulate(Sum, Entries[Index].Position);
    }
    Vector const Centroid = DivideByScalar(Sum, (float)(Count));

    // Same uninitialised-best case as SplitEntries; zero here.
    int BestScore = 9999;
    FSpatialPlane Best;
    memset(&Best, 0, sizeof(Best));
    for (int Index = 0; Index < Count; ++Index)
    {
        Vector const Delta = Subtract(Entries[Index].Position, Centroid);
        if (LengthSquared(Delta) < 0.05)
        {
            continue;
        }
        FSpatialPlane Candidate;
        Candidate.Build(Centroid, Delta);
        FSpatialSplit Split;
        ClassifyVector(Split, Candidate, Entries);
        int const Score = (int)ScoreCounts(Split.Counts);
        if (Score < BestScore) // signed (JGE)
        {
            Best = Candidate;
            BestScore = Score;
            if (Score == 0)
            {
                break;
            }
        }
    }

    for (int Index = 0; Index < Count; ++Index)
    {
        FSpatialEntry const& Entry = Entries[Index];
        int const Side = Best.Classify(Entry.Position, Entry.Radius);
        if (Side == 1)
        {
            Beyond.push_back(Entry);
        }
        else if (Side == 2)
        {
            Behind.push_back(Entry);
        }
        else
        {
            Touching.push_back(Entry);
        }
    }
    Node->Plane = Best;

    for (size_t Index = 0; Index < Touching.size(); ++Index)
    {
        FSpatialEntry const& Entry = Touching[Index];
        int const Side = Node->Plane.Classify(Entry.Position, Entry.Radius);
        FSpatialNode* Child = Node->Children[Side];
        if (Child != nullptr)
        {
            InsertSplit(Child, &Entry);
        }
        else
        {
            Child = GSpatialNodes.PushBack();
            Node->Children[Side] = Child;
            Child->AddEntry(Entry);
            ++EntryCount;
        }
    }
    if (!Beyond.empty())
    {
        FSpatialNode* const Child = GSpatialNodes.PushBack();
        Node->Children[1] = Child;
        Build(Child, Beyond);
    }
    if (!Behind.empty())
    {
        FSpatialNode* const Child = GSpatialNodes.PushBack();
        Node->Children[2] = Child;
        Build(Child, Behind);
    }
}

int BspPartition::QueryFrustum(Geometry::Frustum const* Frustum, FSpatialQueryResult* Result)
{
    // The frustum's bounding sphere, then this object's own sphere query (vtbl+0x1c).
    // FCOMPP AngleY / AngleX, TEST AH,5 / JP: AngleY unless it is (ordered) below AngleX.
    float const Angle = !(Frustum->AngleY < Frustum->AngleX) ? Frustum->AngleY : Frustum->AngleX;
    double const Half = std::tan((double)(Angle) * 0.5) * Frustum->Distance;
    float Radius;
    double Offset;
    // FCOMP / TEST AH,1 / JNZ: C0 is set for below and for unordered.
    if (!((double)(Frustum->Distance) >= Half))
    {
        Radius = (float)(Half);
        Offset = Frustum->Distance;
    }
    else
    {
        double const R = (Half * Half + (double)(Frustum->Distance) * Frustum->Distance) /
                         ((double)(Frustum->Distance) + Frustum->Distance);
        Radius = (float)(R);
        Offset = R;
    }
    Matrix const& M = Frustum->Xform.Rotation;
    float const OffsetX = (float)(M.E[0][2] * Offset);
    double const OffsetY = Offset * M.E[1][2];
    float const OffsetZ = (float)(M.E[2][2] * Offset);
    Geometry::Sphere Bounds;
    Bounds.Center.x = Frustum->Xform.Translation.x - OffsetX;
    Bounds.Center.y = (float)(Frustum->Xform.Translation.y - OffsetY);
    Bounds.Center.z = Frustum->Xform.Translation.z - OffsetZ;
    Bounds.Radius = Radius;
    return QuerySphere(&Bounds, Result);
}

int BspPartition::QuerySphere(Geometry::Sphere const* Sphere, FSpatialQueryResult* Result)
{
    GSpatialQueryStatB = 0;
    GSpatialQueryStatA = 0;
    FSpatialNode* Node = Root;
    while (Node != nullptr)
    {
        if (!Node->IsSplit())
        {
            for (int Index = 0; Index < 8; ++Index)
            {
                FSpatialEntry const& Slot = Node->Slots[Index];
                if (Slot.Object == nullptr)
                {
                    break;
                }
                Vector const Delta = Subtract(Slot.Position, Sphere->Center);
                double const Reach = (double)(Slot.Radius) + Sphere->Radius;
                double const DistSq = ((double)Delta.x * Delta.x + (double)Delta.y * Delta.y) +
                                      (double)Delta.z * Delta.z;
                if (Reach * Reach > DistSq)
                {
                    AppendResult(Result, Slot.Object);
                }
            }
            break;
        }
        double const S = Dot(Sphere->Center, Node->Plane.Normal) - Node->Plane.Distance;
        if (S > Sphere->Radius)
        {
            QuerySphereNode(Node->Children[1], Sphere, Result);
        }
        else if (S < -(double)(Sphere->Radius))
        {
            QuerySphereNode(Node->Children[2], Sphere, Result);
        }
        else
        {
            QuerySphereNode(Node->Children[1], Sphere, Result);
            QuerySphereNode(Node->Children[2], Sphere, Result);
        }
        Node = Node->Children[0];
    }
    return (int)Result->Count;
}

void BspPartition::QuerySphereNode(FSpatialNode* Node, Geometry::Sphere const* Sphere, FSpatialQueryResult* Result)
{
    while (Node != nullptr)
    {
        if (!Node->IsSplit())
        {
            for (int Index = 0; Index < 8; ++Index)
            {
                FSpatialEntry const& Slot = Node->Slots[Index];
                if (Slot.Object == nullptr)
                {
                    return;
                }
                // The differences stay unrounded on the x87 stack here.
                double const Dx = (double)(Slot.Position.x) - Sphere->Center.x;
                double const Dy = (double)(Slot.Position.y) - Sphere->Center.y;
                double const Dz = (double)(Slot.Position.z) - Sphere->Center.z;
                double const Reach = (double)(Slot.Radius) + Sphere->Radius;
                double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
                if (Reach * Reach > DistSq)
                {
                    AppendResult(Result, Slot.Object);
                }
            }
            return;
        }
        double const S = (((double)(Sphere->Center.z) * Node->Plane.Normal.z +
                           (double)(Sphere->Center.y) * Node->Plane.Normal.y) +
                          (double)(Node->Plane.Normal.x) * Sphere->Center.x) - Node->Plane.Distance;
        if (S > Sphere->Radius)
        {
            QuerySphereNode(Node->Children[1], Sphere, Result);
        }
        else if (S < -(double)(Sphere->Radius))
        {
            QuerySphereNode(Node->Children[2], Sphere, Result);
        }
        else
        {
            QuerySphereNode(Node->Children[1], Sphere, Result);
            QuerySphereNode(Node->Children[2], Sphere, Result);
        }
        Node = Node->Children[0];
    }
}

int BspPartition::QueryRay(FSpatialRay const* Ray, FSpatialQueryResult* Result)
{
    GSpatialQueryStatB = 0;
    GSpatialQueryStatA = 0;
    FSpatialNode* const Node = Root;
    if (Node == nullptr)
    {
        return (int)Result->Count;
    }

    if (!Node->IsSplit())
    {
        for (int Index = 0; Index < 8; ++Index)
        {
            FSpatialEntry const& Slot = Node->Slots[Index];
            if (Slot.Object == nullptr)
            {
                break;
            }
            Vector const Delta = Subtract(Slot.Position, Ray->Origin);
            if (RayHitsEntry(Ray, Slot, Delta, Length(Delta)))
            {
                AppendResult(Result, Slot.Object);
            }
        }
        return (int)Result->Count;
    }

    if (Node->Children[0] != nullptr)
    {
        QueryRayNode(Node->Children[0], Ray, Result);
    }
    Vector Hit;
    int Side = 0;
    if (!IntersectRayPlane(Ray, &Node->Plane, &Hit, &Side))
    {
        if (Node->Children[Side] != nullptr)
        {
            QueryRayNode(Node->Children[Side], Ray, Result);
        }
        return (int)Result->Count;
    }

    int const Other = Side ^ 3;
    if (Node->Children[Side] != nullptr)
    {
        // The near side up to the hit point.
        FSpatialRay Near;
        Near.Origin = Ray->Origin;
        Near.Direction = Ray->Direction;
        Vector const ToHit = Subtract(Hit, Ray->Origin);
        Near.Length = (float)(std::sqrt(((double)(ToHit.z) * ToHit.z +
                                                    (double)(ToHit.x) * ToHit.x) +
                                                   (double)(ToHit.y) * ToHit.y));
        QueryRayNode(Node->Children[Side], &Near, Result);
    }
    if (Node->Children[Other] != nullptr)
    {
        // The far side from the hit point, with the full length again.
        FSpatialRay Far;
        Far.Origin = Hit;
        Far.Direction = Ray->Direction;
        Far.Length = Ray->Length;
        QueryRayNode(Node->Children[Other], &Far, Result);
    }
    return (int)Result->Count;
}

void BspPartition::QueryRayNode(FSpatialNode* Node, FSpatialRay const* Ray, FSpatialQueryResult* Result)
{
    while (Node != nullptr)
    {
        if (!Node->IsSplit())
        {
            for (int Index = 0; Index < 8; ++Index)
            {
                FSpatialEntry const& Slot = Node->Slots[Index];
                if (Slot.Object == nullptr)
                {
                    return;
                }
                Vector const Delta = Subtract(Slot.Position, Ray->Origin);
                double const Dist = std::sqrt(((double)Delta.y * Delta.y + (double)Delta.z * Delta.z) +
                                              (double)Delta.x * Delta.x);
                if (RayHitsEntry(Ray, Slot, Delta, Dist))
                {
                    AppendResult(Result, Slot.Object);
                }
            }
            return;
        }

        if (Node->Children[0] != nullptr)
        {
            QueryRayNode(Node->Children[0], Ray, Result);
        }
        double const S = Dot(Ray->Origin, Node->Plane.Normal) - Node->Plane.Distance;
        double const Dn = ((double)(Ray->Direction.z) * Node->Plane.Normal.z +
                           (double)(Ray->Direction.y) * Node->Plane.Normal.y) +
                          (double)(Ray->Direction.x) * Node->Plane.Normal.x;
        int Side;
        double Num;
        double Den;
        if (S > 0.0)
        {
            Side = 1;
            Num = S;
            Den = -Dn;
        }
        else
        {
            Side = 2;
            Num = -S;
            Den = Dn;
        }
        bool bCrosses = false;
        float T = 0.0f;
        if (!(Den < 0.001))
        {
            T = (float)(Num / Den);
            bCrosses = !(T < 0.0f) && !(T > Ray->Length);
        }
        if (!bCrosses)
        {
            Node = Node->Children[Side];
            continue;
        }

        Vector const Hit = AddVectors(Ray->Origin, Scale(Ray->Direction, T));
        int const Other = Side ^ 3;
        FSpatialNode* const NearChild = Node->Children[Side];
        if (NearChild != nullptr)
        {
            // Unrounded differences on the x87 stack; only the length is stored.
            double const Dx = (double)(Hit.x) - Ray->Origin.x;
            double const Dy = (double)(Hit.y) - Ray->Origin.y;
            double const Dz = (double)(Hit.z) - Ray->Origin.z;
            FSpatialRay Near;
            Near.Origin = Ray->Origin;
            Near.Direction = Ray->Direction;
            Near.Length = (float)(std::sqrt((Dz * Dz + Dx * Dx) + Dy * Dy));
            QueryRayNode(NearChild, &Near, Result);
        }
        FSpatialNode* const FarChild = Node->Children[Other];
        if (FarChild == nullptr)
        {
            return;
        }
        FSpatialRay Far;
        Far.Origin = Hit;
        Far.Direction = Ray->Direction;
        Far.Length = Ray->Length;
        QueryRayNode(FarChild, &Far, Result);
        return;
    }
}

void __cdecl FreeSpatial(ISpatialPartition* Spatial)
{
    // .text:06331940: the scalar deleting destructor with flag 1 when the pointer is set.
    if (Spatial != nullptr)
    {
        delete Spatial;
    }
}

ISpatialPartition* __cdecl NewSpatial(char const* Type)
{
    // .text:0632e9f0: DACOM stricmp against "BSP", "Linear" and "QuadTree"; the last match has
    // no partition behind it and returns 0 like an unknown name.
    if (CallDacomStricmp(Type, "BSP") == 0)
    {
        return new BspPartition();
    }
    if (CallDacomStricmp(Type, "Linear") == 0)
    {
        return new LinearPartition();
    }
    CallDacomStricmp(Type, "QuadTree");
    return nullptr;
}
