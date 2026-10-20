// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "screen_quad_state.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "common_csys.h"
#include "DALib.h"
#include "dacom.h"            // DACOM_CRC::GetCRC32
#include "globals.h"          // G_RenderSurfaceWidth / G_RenderSurfaceHeight (DAT_00610850 / 854)
#include "render/IRenderPipeline.h"
#include "space_camera.h"
#include "win3d_immediate.h"  // Win3DPushProjection / Win3DSetOrtho2D / Win3DPopProjection
#include "x86math.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <utility>
#include <windows.h>

// DAT_006101d8 -- the default texture frame {no texture, u0 0, v0 0, u1 1, v1 1} (.data, never
// written). Copied into a new container's key by FUN_00402f40 and FUN_00403ea0, and handed to
// FUN_00402e30 by the core bars (0x004d54a2) and the IME background (0x00563f91).
unsigned long const G_ScreenQuadDefaultFrame[5] = {0u, 0u, 0u, 0x3f800000u, 0x3f800000u};

namespace
{
    static const int kMaxCachedRenderStates = 64;
    static const int kMaxCachedPipelineStates = 16;
    static const int kMaxCachedTextureStageStates = 32;
    static const unsigned kScreenQuadFvf = 0x1c4u; // XYZRHW|DIFFUSE|SPECULAR|TEX1

    struct FCachedIntPair
    {
        int Key;
        int Value;
        unsigned char bValid;
    };

    FCachedIntPair GRenderStateCache[kMaxCachedRenderStates];
    FCachedIntPair GPipelineStateCache[kMaxCachedPipelineStates];
    FCachedIntPair GTextureStageCache[kMaxCachedTextureStageStates];

    // FVF 0x1C4 vertex — FUN_004042c0 expand shape (8 dwords / 0x20).
    struct FScreenQuadGpuVert
    {
        float X;
        float Y;
        float Z;
        float Rhw;
        unsigned Color;
        unsigned Specular;
        float U;
        float V;
    };

    // Retail material node — exactly 0x28 (FUN_00403160 / FUN_00402ed0).
    // +0x08/+0x0c/+0x10 = AABB record buffer (stride 0x2C); +0x14/+0x18/+0x1c =
    // rotated FVF verts (stride 0x20); +0x20 texture key; +0x24 blend.
    struct FScreenQuadBatchNode
    {
        FScreenQuadBatchNode* Next;        // +0x00
        FScreenQuadBatchNode* Prev;        // +0x04
        FScreenQuadRecord* Records;        // +0x08
        int RecordCount;                   // +0x0c
        int RecordCapacity;                // +0x10
        FScreenQuadGpuVert* RotatedVerts;  // +0x14
        int RotVertCount;                  // +0x18
        int RotVertCapacity;               // +0x1c
        void* TextureHandle;               // +0x20
        int BlendMode;                     // +0x24
    };
    static_assert(sizeof(FScreenQuadBatchNode) == 0x28, "batch node 0x28");

    // DAT_00616498 batch container (map value body). Free list at +0x00,
    // live list at +0x0c, current at +0x18, 5-dword material key at +0x1c.
    struct FScreenQuadBatchContainer
    {
        FScreenQuadBatchNode* FreeHead; // +0x00
        FScreenQuadBatchNode* FreeTail; // +0x04
        int FreeCount;                  // +0x08
        FScreenQuadBatchNode* LiveHead; // +0x0c
        FScreenQuadBatchNode* LiveTail; // +0x10
        int LiveCount;                  // +0x14
        FScreenQuadBatchNode* Current;  // +0x18
        unsigned MatKey[5];             // +0x1c..+0x2c (key[0]=tex; [1..4]=UV floats)
        int Sentinel;                   // +0x30 = -1
    };

    // DAT_00616498 -- the container of the current material key inside the map DAT_00616478
    // (defined below, after the container destructor), or null.
    FScreenQuadBatchContainer* G_ActiveContainer = nullptr;

    // DAT_00616474 — FUN_00402d30 material-list key; change nulls DAT_00616498.
    unsigned long G_MaterialListKey = 0;
    // DAT_00616490 — default key restored by FUN_00402d50 / FUN_00403ea0 epilogue.
    unsigned long G_MaterialListKeyDefault = 0;

    // DAT_00616494 — lazy 3DMathEngine (FUN_005b73e0 CreateInstance "3DMathEngine").
    I3DMathEngine* G_ScreenQuadMath = nullptr;


    bool CacheLookupOrStore(FCachedIntPair* Table, int Capacity, int Key, int Value, bool* bChanged)
    {
        int FreeSlot = -1;
        for (int Index = 0; Index < Capacity; ++Index)
        {
            if (Table[Index].bValid == 0)
            {
                if (FreeSlot < 0)
                    FreeSlot = Index;
                continue;
            }
            if (Table[Index].Key != Key)
                continue;
            if (Table[Index].Value == Value)
            {
                *bChanged = false;
                return true;
            }
            Table[Index].Value = Value;
            *bChanged = true;
            return true;
        }
        if (FreeSlot < 0)
        {
            *bChanged = true;
            return false;
        }
        Table[FreeSlot].Key = Key;
        Table[FreeSlot].Value = Value;
        Table[FreeSlot].bValid = 1;
        *bChanged = true;
        return true;
    }

    void ClearCache(FCachedIntPair* Table, int Capacity)
    {
        memset(Table, 0, static_cast<size_t>(Capacity) * sizeof(FCachedIntPair));
    }

    I3DMathEngine* AcquireScreenQuadMath()
    {
        // FUN_005b73e0 → CreateInstance("3DMathEngine"); cached as DAT_00616494.
        if (G_ScreenQuadMath == nullptr)
            G_ScreenQuadMath = Acquire3DMathEngine();
        return G_ScreenQuadMath;
    }

    // FUN_00403160 (thiscall on the node, 0x00403160..0x00403187) -- every dword zeroed, then
    // +0xc and +0x18 zeroed again. No null check in retail.
    void InitBatchNode(FScreenQuadBatchNode* Node)
    {
        Node->Next = nullptr;
        Node->Prev = nullptr;
        Node->Records = nullptr;
        Node->RecordCount = 0;
        Node->RecordCapacity = 0;
        Node->RotatedVerts = nullptr;
        Node->RotVertCount = 0;
        Node->RotVertCapacity = 0;
        Node->TextureHandle = nullptr;
        Node->BlendMode = 0;
        Node->RecordCount = 0;
        Node->RotVertCount = 0;
    }

    // FUN_004031e0 (thiscall on the node, 0x004031e0..0x004031f9) -- FUN_005b7e1d on the
    // buffers at +0x14 and +0x08; the fields are not cleared (the sole caller FUN_00403190
    // frees the node next). No null check in retail.
    void ReleaseNodeBuffers(FScreenQuadBatchNode* Node)
    {
        operator delete(Node->RotatedVerts);
        operator delete(Node->Records);
    }

    // FUN_00402ed0 (thiscall on the free list at container+0, 0x00402ed0..0x00402f3e) -- the
    // free head unlinked (count - 1), or with none operator new(0x28) zeroed as FUN_00403160;
    // a failed new returns 0 (0x00402f12 CMP EAX,ESI / JZ -- the VC6 operator new returns null,
    // hence nothrow here).
    FScreenQuadBatchNode* AllocBatchNode(FScreenQuadBatchContainer* Pool)
    {
        FScreenQuadBatchNode* Node = Pool->FreeHead;
        if (Node == nullptr)
        {
            Node = static_cast<FScreenQuadBatchNode*>(operator new(0x28u, std::nothrow));
            if (Node == nullptr)
                return nullptr;
            InitBatchNode(Node);
            return Node;
        }
        if (Node->Prev == nullptr)
            Pool->FreeHead = Node->Next;
        else
            Node->Prev->Next = Node->Next;
        if (Node->Next == nullptr)
            Pool->FreeTail = Node->Prev;
        else
            Node->Next->Prev = Node->Prev;
        Pool->FreeCount -= 1;
        return Node;
    }

    // FUN_00404580 (thiscall on the free list at container+0, RET 4) -- Node appended at the
    // tail; 0x00404584: a null node is ignored (the only check retail makes).
    void SpliceOntoFreeList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        if (Node == nullptr)
            return;
        FScreenQuadBatchNode* OldTail = Pool->FreeTail;
        Pool->FreeTail = Node;
        if (OldTail == nullptr)
        {
            Pool->FreeHead = Node;
            Node->Next = nullptr;
            Node->Prev = nullptr;
        }
        else
        {
            OldTail->Next = Node;
            Node->Next = nullptr;
            Node->Prev = OldTail;
        }
        Pool->FreeCount += 1;
    }

    // FUN_00404d90 (thiscall on a {head, tail, count} list, RET 4; here the live list at
    // container+0xc) -- Node unlinked, count - 1. No null checks in retail.
    void UnlinkFromLiveList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        if (Node->Prev == nullptr)
            Pool->LiveHead = Node->Next;
        else
            Node->Prev->Next = Node->Next;
        if (Node->Next == nullptr)
            Pool->LiveTail = Node->Prev;
        else
            Node->Next->Prev = Node->Prev;
        Pool->LiveCount -= 1;
    }

    // FUN_00403190 (thiscall on the free list, 0x00403190..0x004031da) -- each head unlinked
    // (count - 1), FUN_004031e0 on it, then the node freed. No null check in retail.
    void DestroyFreeList(FScreenQuadBatchContainer* Pool)
    {
        while (Pool->FreeHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->FreeHead;
            if (Node->Prev == nullptr)
                Pool->FreeHead = Node->Next;
            else
                Node->Prev->Next = Node->Next;
            if (Node->Next == nullptr)
                Pool->FreeTail = Node->Prev;
            else
                Node->Next->Prev = Node->Prev;
            Pool->FreeCount -= 1;
            ReleaseNodeBuffers(Node);
            operator delete(Node);
        }
    }

    // FUN_00403200 (thiscall on the container, 0x00403200..0x0040329f) -- every live head
    // unlinked and appended to the free tail (inline FUN_00404d90 / FUN_00404580), then every
    // free head unlinked and freed with its +0x14 / +0x08 buffers. No null check, and +0x18 is
    // left as it is.
    void DestroyBatchContainer(FScreenQuadBatchContainer* Pool)
    {
        while (Pool->LiveHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->LiveHead;
            UnlinkFromLiveList(Pool, Node);
            SpliceOntoFreeList(Pool, Node);
        }
        while (Pool->FreeHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->FreeHead;
            if (Node->Prev == nullptr)
                Pool->FreeHead = Node->Next;
            else
                Node->Prev->Next = Node->Next;
            if (Node->Next == nullptr)
                Pool->FreeTail = Node->Prev;
            else
                Node->Next->Prev = Node->Prev;
            Pool->FreeCount -= 1;
            operator delete(Node->RotatedVerts);
            operator delete(Node->Records);
            operator delete(Node);
        }
    }

    // FUN_004032a0 (0x004032a0..0x00403342) -- FUN_00403200's body on the container at
    // this + 4 (the map node's value). No null check in retail.
    void DestroyBatchContainerEmbedded(void* Owner)
    {
        FScreenQuadBatchContainer* Pool =
            reinterpret_cast<FScreenQuadBatchContainer*>(static_cast<unsigned char*>(Owner) + 4);
        DestroyBatchContainer(Pool);
    }

    // The value of one DAT_00616478 map node: the container at node+0x10. Its destructor is the
    // map's value destructor FUN_00405820 = FUN_004032a0 on the container (every live node moved
    // to the free list, then every free node and its two buffers freed). Retail builds a
    // temporary {empty lists, current 0, +0x30 = -1} (FUN_004045b0 / the inline copy in
    // FUN_00402f40) and inserts a copy of it; constructing the entry in place gives the same
    // value. The key words +0x1c..+0x2c of that temporary are never initialised in retail; every
    // insert path overwrites them from DAT_006101d8 before they are read.
    struct FScreenQuadBatchEntry
    {
        FScreenQuadBatchContainer Container;

        FScreenQuadBatchEntry()
        {
            Container.FreeHead = nullptr;
            Container.FreeTail = nullptr;
            Container.FreeCount = 0;
            Container.LiveHead = nullptr;
            Container.LiveTail = nullptr;
            Container.LiveCount = 0;
            Container.Current = nullptr;
            for (int Index = 0; Index < 5; ++Index)
                Container.MatKey[Index] = 0;
            Container.Sentinel = -1;
        }

        ~FScreenQuadBatchEntry()
        {
            DestroyBatchContainer(&Container);
        }

        FScreenQuadBatchEntry(FScreenQuadBatchEntry const&) = delete;
        FScreenQuadBatchEntry& operator=(FScreenQuadBatchEntry const&) = delete;
    };

    // DAT_00616478 -- std::map<material-list key, container> (head DAT_0061647c, size
    // DAT_00616488). Static init LAB_00402c10 (FUN_00404dd0 + atexit LAB_00402c40, which
    // destroys every node through FUN_00405820): the map's own constructor / destructor.
    std::map<unsigned long, FScreenQuadBatchEntry> G_BatchMap;


    // FUN_00404d50 as the insert paths use it (the live list at container+0xc, before node 0):
    // Node goes to the live tail.
    void AppendToLiveList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        FScreenQuadBatchNode* const OldTail = Pool->LiveTail;
        Pool->LiveTail = Node;
        if (OldTail == nullptr)
            Pool->LiveHead = Node;
        else
            OldTail->Next = Node;
        Node->Next = nullptr;
        Node->Prev = OldTail;
        Pool->LiveCount += 1;
    }

    // FUN_004045d0 (thiscall on a container, RET 0xc: frame, blend, bFind) -- the body
    // FUN_00402e30 inlines after FUN_00402f40. The frame's five dwords become the container key
    // (+0x1c); the current node is kept when its texture (+0x20) matches key[0], otherwise it is
    // dropped (bFind 0) or replaced by the first live node that matches (bFind 1); with none, a
    // node from FUN_00402ed0 goes to the live tail with the texture, the blend and empty counts.
    FScreenQuadBatchNode* ContainerBeginBatch(FScreenQuadBatchContainer* C,
                                              unsigned long const* Frame, int BlendMode,
                                              bool bFind)
    {
        for (int Index = 0; Index < 5; ++Index)
            C->MatKey[Index] = static_cast<unsigned>(Frame[Index]);
        void* const Texture = reinterpret_cast<void*>(static_cast<uintptr_t>(C->MatKey[0]));
        if (C->Current != nullptr && C->Current->TextureHandle != Texture)
        {
            if (!bFind)
            {
                C->Current = nullptr;
            }
            else
            {
                C->Current = C->LiveHead;
                while (C->Current != nullptr && C->Current->TextureHandle != Texture)
                    C->Current = C->Current->Next;
            }
        }
        if (C->Current == nullptr)
        {
            FScreenQuadBatchNode* const Node = AllocBatchNode(C);
            C->Current = Node;
            AppendToLiveList(C, Node);
            Node->TextureHandle = Texture;
            Node->BlendMode = BlendMode;
            Node->RecordCount = 0;
            Node->RotVertCount = 0;
        }
        return C->Current;
    }

    // FUN_00402f40 (.text:00402f40-0040315b) -- DAT_00616498, found or inserted.
    FScreenQuadBatchContainer* EnsureBatchContainer()
    {
        // 0x00402f4e..0x00402f65: already set -> returned as it is.
        if (G_ActiveContainer != nullptr)
            return G_ActiveContainer;

        // 0x00402f6b..0x00403086: the pair {DAT_00616474, empty container} inserted
        // (FUN_00404f20); an existing entry for the key is left untouched. The temporaries are
        // destroyed empty. DAT_00616498 = the entry's container (node + 0x10).
        std::pair<std::map<unsigned long, FScreenQuadBatchEntry>::iterator, bool> const Inserted =
            G_BatchMap.try_emplace(G_MaterialListKey);
        FScreenQuadBatchContainer* const C = &Inserted.first->second.Container;
        G_ActiveContainer = C;
        if (!Inserted.second)
            return C;

        // 0x00403091..0x004030ab: a new entry takes the default frame as its key.
        for (int Index = 0; Index < 5; ++Index)
            C->MatKey[Index] = static_cast<unsigned>(G_ScreenQuadDefaultFrame[Index]);
        void* const Texture = reinterpret_cast<void*>(static_cast<uintptr_t>(C->MatKey[0]));

        // 0x004030ad..0x004030cf: a current node of another texture is replaced by the first
        // live node of this one (a new entry has neither).
        if (C->Current != nullptr && C->Current->TextureHandle != Texture)
        {
            C->Current = C->LiveHead;
            while (C->Current != nullptr && C->Current->TextureHandle != Texture)
                C->Current = C->Current->Next;
        }

        // 0x004030d1..0x0040313b: none -- the free list's head (FUN_00404d90) or
        // new(0x28) + FUN_00403160, at the live tail (FUN_00404d50); +0x20 = key[0], +0x24 = 0,
        // +0xc = +0x18 = 0.
        if (C->Current == nullptr)
        {
            FScreenQuadBatchNode* const Node = AllocBatchNode(C);
            C->Current = Node;
            AppendToLiveList(C, Node);
            Node->TextureHandle = Texture;
            Node->BlendMode = 0;
            Node->RecordCount = 0;
            Node->RotVertCount = 0;
        }
        return C;
    }

    // The container's UV rect, key words +0x20..+0x2c, read as dwords (FUN_004034a0,
    // FUN_004038f0). The container always exists when these are read (FUN_00402f40 never
    // returns null).
    float BatchU0(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[1], sizeof(V));
        return V;
    }
    float BatchV0(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[2], sizeof(V));
        return V;
    }
    float BatchU1(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[3], sizeof(V));
        return V;
    }
    float BatchV1(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[4], sizeof(V));
        return V;
    }
    // The record append retail inlines (FUN_004038f0 0x00403bd9..0x00403c97 /
    // 0x00403ddb..0x00403e87, FUN_004034a0 0x004035e6..0x004036a8, LAB_004137a0): count + 1;
    // past the capacity (signed JLE) a block of count + 1 + 0x20 records from operator new, the
    // old records copied when there were any, the old block freed (FUN_005b7e1d, always); the
    // 0x2c record written at the old count.
    void AppendRecordToNode(FScreenQuadBatchNode* Node, FScreenQuadRecord const& Record)
    {
        int const Count = Node->RecordCount;
        Node->RecordCount = Count + 1;
        if (Count + 1 > Node->RecordCapacity)
        {
            Node->RecordCapacity = Count + 1 + 0x20;
            FScreenQuadRecord* const Grown = static_cast<FScreenQuadRecord*>(
                operator new(static_cast<size_t>(Node->RecordCapacity) * 0x2cu));
            if (Node->Records != nullptr)
                memcpy(Grown, Node->Records, static_cast<size_t>(Count) * 0x2cu);
            operator delete(Node->Records);   // FUN_005b7e1d
            Node->Records = Grown;
        }
        Node->Records[Count] = Record;
    }

    // One FVF 0x1c4 vertex of an axis-aligned record, as FUN_004042c0 writes it: Z = +0x10,
    // RHW = +0x14, the colour dword +0x28 copied raw, specular 0.
    void WriteRecordVertex(FScreenQuadGpuVert* Out, FScreenQuadRecord const& Rec, float X, float Y,
                           float U, float V)
    {
        Out->X = X;
        Out->Y = Y;
        Out->Z = Rec.DepthA;
        Out->Rhw = Rec.DepthB;
        Out->Color = Rec.ColorBits;
        Out->Specular = 0;
        Out->U = U;
        Out->V = V;
    }

    // FUN_004042c0 (thiscall on a node, RET 0x10: pipeline, vertex-buffer manager, index buffer,
    // draw). Listing-traced 0x004042c0..0x00404573. No argument is null-checked in retail.
    void FlushOneBatch(FScreenQuadBatchNode* Node, IRenderPipeline* Pipe,
                       IVertexBufferManager* Vbm, IRPIndexBuffer* Ib, IRPDraw* Draw)
    {
        // 0x004042c7..0x004042d6: slot 66 (+0x108) with the node's texture, unconditionally.
        Pipe->SetTextureStageTexture(0, Node->TextureHandle);

        // 0x004042dc..0x00404318: blend 1 -> SRCBLEND / DESTBLEND 2 / 2, blend 2 -> 5 / 6,
        // anything else leaves them alone. 0x0040431b..0x0040432b: ALPHABLENDENABLE = blend != 0.
#ifdef FREELANCER_WALK_MODE
        int const BlendMode = Node->BlendMode == 0 && BaseWalkGetInterfaceDrawOpacity() < 1.0f ?
            2 : Node->BlendMode;
#else
        int const BlendMode = Node->BlendMode;
#endif
        if (BlendMode == 1)
        {
            Pipe->SetRenderState(D3DRS_SRCBLEND, 2);
            Pipe->SetRenderState(D3DRS_DESTBLEND, 2);
        }
        else if (BlendMode == 2)
        {
            Pipe->SetRenderState(D3DRS_SRCBLEND, 5);
            Pipe->SetRenderState(D3DRS_DESTBLEND, 6);
        }
        Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, (BlendMode != 0) ? 1 : 0);

        // 0x00404331..0x00404340: rotated vertices + four per record; nothing to draw -> the
        // epilogue.
        unsigned int const VertexCount =
            static_cast<unsigned int>(Node->RotVertCount + Node->RecordCount * 4);
        if (VertexCount != 0)
        {
            // 0x00404346..0x0040435e: IVertexBufferManager slot 5 (+0x14) with FVF 0x1c4 into
            // the 4-dword lock block ([2] = the base vertex, [3] = the write pointer).
            FVertexMemoryLock Lock;
            if (Vbm->LockVertexMemory(kScreenQuadFvf, VertexCount, &Lock) >= 0)
            {
                // 0x00404364..0x00404473: each record -> (X0,Y0,U0,V0) (X0,Y1,U0,V1)
                // (X1,Y1,U1,V1) (X1,Y0,U1,V0); the count is re-read every pass.
                FScreenQuadGpuVert* Dst =
                    (FScreenQuadGpuVert*)Lock.Vertices;
                for (int Index = 0; Index < Node->RecordCount; ++Index)
                {
                    FScreenQuadRecord const& Rec = Node->Records[Index];
                    WriteRecordVertex(Dst + 0, Rec, Rec.X0, Rec.Y0, Rec.U0, Rec.V0);
                    WriteRecordVertex(Dst + 1, Rec, Rec.X0, Rec.Y1, Rec.U0, Rec.V1);
                    WriteRecordVertex(Dst + 2, Rec, Rec.X1, Rec.Y1, Rec.U1, Rec.V1);
                    WriteRecordVertex(Dst + 3, Rec, Rec.X1, Rec.Y0, Rec.U1, Rec.V0);
                    Dst += 4;
                }

                // 0x0040447d..0x00404497: the rotated vertices copied after them (REP MOVSD/B).
                memcpy(Dst, Node->RotatedVerts,
                       static_cast<size_t>(Node->RotVertCount) * sizeof(FScreenQuadGpuVert));

#ifdef FREELANCER_WALK_MODE
                // Fade the GPU copy only: cached font records and rotated atlas icons keep their colours.
                FScreenQuadGpuVert* const Vertices =
                    static_cast<FScreenQuadGpuVert*>(Lock.Vertices);
                for (unsigned int Index = 0; Index < VertexCount; ++Index)
                    Vertices[Index].Color = BaseWalkFadeInterfaceColor(Vertices[Index].Color,
                                                                     BlendMode == 1);
#endif

                // 0x00404499..0x0040449d: slot 6 (+0x18).
                Vbm->UnlockVertexMemory(&Lock);

                // 0x004044a0..0x004044d1: quads = count >> 2, six indices each, from the
                // index buffer's scratch (-1) through slot 7 (+0x1c); offset and pointer start 0.
                unsigned int const QuadCount = VertexCount >> 2;
                unsigned int const IndexCount = QuadCount * 6u;
                void* const ScratchIb = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
                unsigned int IndexOffset = 0;
                void* Indices = nullptr;
                if (Ib->LockIb(ScratchIb, &IndexOffset, &Indices, IndexCount) >= 0)
                {
                    // 0x004044d7..0x00404527: {b, b+1, b+2, b, b+2, b+3}, b stepping by 4.
                    unsigned short* Out = static_cast<unsigned short*>(Indices);
                    unsigned int Base = 0;
                    for (unsigned int Quad = 0; Quad < QuadCount; ++Quad)
                    {
                        Out[0] = static_cast<unsigned short>(Base);
                        Out[1] = static_cast<unsigned short>(Base + 1u);
                        Out[2] = static_cast<unsigned short>(Base + 2u);
                        Out[3] = static_cast<unsigned short>(Base);
                        Out[4] = static_cast<unsigned short>(Base + 2u);
                        Out[5] = static_cast<unsigned short>(Base + 3u);
                        Out += 6;
                        Base += 4u;
                    }

                    // 0x00404529..0x00404558: slot 8 (+0x20); slot 9 (+0x24) with the lock's
                    // base vertex; on success IRPDraw slot 3 (+0xc) triangle list over
                    // VertexCount vertices, IndexCount indices from the locked offset.
                    Ib->UnlockIb(ScratchIb);
                    if (Ib->SelectIb(ScratchIb, static_cast<unsigned int>(Lock.Listing.BaseVertex), 0, 0) >= 0)
                        Draw->DrawIndexedPrimitive(4, 0, VertexCount, IndexOffset, IndexCount);
                }
            }
        }

        // 0x0040455f..0x00404566: ALPHABLENDENABLE off on every path.
        Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    }

    // FUN_00403ea0 0x004040df..0x0040415a / 0x00404186..0x00404200: every live node with
    // anything queued goes through FUN_004042c0 with the DALib globals (not the caller's
    // pipeline), is unlinked (FUN_00404d90) and put on the free list (FUN_00404580) with its
    // counts untouched; then no current node.
    void FlushContainer(FScreenQuadBatchContainer* C)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        IVertexBufferManager* const Vbm = DALib::VertexBuffer;
        IRPIndexBuffer* const Ib = DALib::RPIndexBuffer;
        IRPDraw* const Draw = DALib::RPDraw;
        while (C->LiveHead != nullptr)
        {
            FScreenQuadBatchNode* const Node = C->LiveHead;
            if (Node->RotVertCount + Node->RecordCount * 4 != 0)
                FlushOneBatch(Node, Pipe, Vbm, Ib, Draw);
            UnlinkFromLiveList(C, Node);
            SpliceOntoFreeList(C, Node);
        }
        C->Current = nullptr;
    }

}

FScreenQuadRenderStateSaver::FScreenQuadRenderStateSaver(IRenderPipeline* InPipe, int InState,
                                                         int Value)
    : Pipe(InPipe), State(InState), Saved(0)
{
    Pipe->GetRenderState((D3DRENDERSTATETYPE)State, &Saved);   // slot 61 (+0xf4)
    Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Value);    // slot 60 (+0xf0)
}

FScreenQuadRenderStateSaver::~FScreenQuadRenderStateSaver()
{
    // FUN_00405840 (inlined at most sites): Saved back through slot 60.
    Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
}

int ScreenQuadClip1D(float* Edge0, float* Edge1, float* Attr0, float* Attr1, float Lo, float Hi,
                     float Span)
{
    // FUN_00404670 (cdecl), listing-traced 0x00404670..0x00404715. No guard on Span: retail
    // divides by it as given.
    // 0x00404675..0x00404684: Edge1 below Lo (TEST AH,5 / JNP -- ordered less only) -> 0.
    if (*Edge1 < Lo)
        return 0;
    // 0x0040468e..0x00404699: Edge0 not below Hi (TEST AH,1 / JZ; unordered passes) -> 0.
    if (static_cast<double>(*Edge0) >= static_cast<double>(Hi))
        return 0;
    int Result = 1;
    // 0x0040469b..0x004046d2: Lo - Edge0 above 0 (TEST AH,0x41 / JNZ skips not-greater and
    // unordered): Edge0 moved to Lo, Attr0 += (Attr1 - Attr0) * (delta / Span); the delta stays
    // on the FPU.
    double const DeltaLo = static_cast<double>(Lo) - static_cast<double>(*Edge0);
    if (DeltaLo > 0.0)
    {
        Result = -1;
        *Edge0 = static_cast<float>(DeltaLo + static_cast<double>(*Edge0));
        *Attr0 = static_cast<float>(
            (static_cast<double>(*Attr1) - static_cast<double>(*Attr0)) *
                (DeltaLo / static_cast<double>(Span)) +
            static_cast<double>(*Attr0));
    }
    // 0x004046d8..0x004046e9: Hi - Edge1 not below 0 (TEST AH,5 / JP: greater, equal and
    // unordered all take the jump) -> Result.
    double const DeltaHi = static_cast<double>(Hi) - static_cast<double>(*Edge1);
    if (!(DeltaHi < 0.0))
        return Result;
    // 0x004046eb..0x00404707: Edge1 moved to Hi, Attr1 += (Attr1 - Attr0) * (delta / Span)
    // with the Attr0 written above; -1.
    *Edge1 = static_cast<float>(DeltaHi + static_cast<double>(*Edge1));
    *Attr1 = static_cast<float>(
        (static_cast<double>(*Attr1) - static_cast<double>(*Attr0)) *
            (DeltaHi / static_cast<double>(Span)) +
        static_cast<double>(*Attr1));
    return -1;
}

void ScreenQuadInvalidateStateCache()
{
    ClearCache(GRenderStateCache, kMaxCachedRenderStates);
    ClearCache(GPipelineStateCache, kMaxCachedPipelineStates);
    ClearCache(GTextureStageCache, kMaxCachedTextureStageStates);
}

void ScreenQuadSetRenderState(IRenderPipeline* Pipe, int State, int Value)
{
    if (Pipe == nullptr)
        return;
    bool bChanged = true;
    CacheLookupOrStore(GRenderStateCache, kMaxCachedRenderStates, State, Value, &bChanged);
    if (bChanged)
        Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Value);
}

void ScreenQuadSetPipelineState(IRenderPipeline* Pipe, int Id, int Value)
{
    if (Pipe == nullptr)
        return;
    bool bChanged = true;
    CacheLookupOrStore(GPipelineStateCache, kMaxCachedPipelineStates, Id, Value, &bChanged);
    if (bChanged)
        Pipe->SetPipelineState(Id, Value);
}

void ScreenQuadSetTextureStageState(IRenderPipeline* Pipe, int Stage, int Type, int Value)
{
    if (Pipe == nullptr)
        return;
    int const Key = (Stage << 16) | (Type & 0xffff);
    bool bChanged = true;
    CacheLookupOrStore(GTextureStageCache, kMaxCachedTextureStageStates, Key, Value, &bChanged);
    if (bChanged)
        Pipe->SetTextureStageState(Stage, Type, Value);
}

void ApplyScreenQuadRenderStates(IRenderPipeline* Pipe)
{
    if (Pipe == nullptr)
        return;
    ScreenQuadSetRenderState(Pipe, 7, 0);
    ScreenQuadSetRenderState(Pipe, 14, 0);
    ScreenQuadSetRenderState(Pipe, 0x16, 1);
    ScreenQuadSetRenderState(Pipe, 0x1b, 1);
    ScreenQuadSetRenderState(Pipe, 0x13, 5);
    ScreenQuadSetRenderState(Pipe, 0x14, 6);
    ScreenQuadSetRenderState(Pipe, 0x89, 0);
    ScreenQuadSetRenderState(Pipe, 0x1c, 0);
    ScreenQuadSetRenderState(Pipe, 0x8d, 1);
    ScreenQuadSetRenderState(Pipe, 0xA8, 0xF);
    ScreenQuadSetPipelineState(Pipe, 0, 1);
    ScreenQuadSetTextureStageState(Pipe, 0, 1, 4);
    ScreenQuadSetTextureStageState(Pipe, 0, 2, 2);
    ScreenQuadSetTextureStageState(Pipe, 0, 3, 0);
    ScreenQuadSetTextureStageState(Pipe, 0, 4, 4);
    ScreenQuadSetTextureStageState(Pipe, 0, 5, 2);
    ScreenQuadSetTextureStageState(Pipe, 0, 6, 0);
    ScreenQuadSetTextureStageState(Pipe, 0, 0xD, 3);
    ScreenQuadSetTextureStageState(Pipe, 0, 0xE, 3);
    ScreenQuadSetTextureStageState(Pipe, 0, 0x12, 0);
    ScreenQuadSetTextureStageState(Pipe, 1, 1, 1);
}

void ScreenQuadSetMaterialKey(unsigned long Key)
{
    // FUN_00402d30 (0x00402d30..0x00402d4b): a different key is stored and DAT_00616498
    // nulled -- nothing else. The previous key's container keeps its queued nodes in the map
    // until a flush names that key.
    if (G_MaterialListKey != Key)
    {
        G_MaterialListKey = Key;
        G_ActiveContainer = nullptr;
    }
}

// FUN_00402d50 (0x00402d50..0x00402e2a; sole caller the font shutdown FUN_004174a0 tail jump
// 0x0041761a). 0x00402d53..0x00402e07: every map node erased -- the clear path (size != 0,
// FUN_00405510 + FUN_00405820 + free per node) and the erase-range path (FUN_00405130) both
// run the value destructor, i.e. FUN_004032a0 on each container. 0x00402e09..0x00402e27: the
// default key restored and DAT_00616498 nulled only when the key differs from it. With the
// default key already current, retail leaves DAT_00616498 pointing into the freed node when it
// was set; ported as retail has it.
void ScreenQuadResetDrawBatchCache()
{
    G_BatchMap.clear();
    if (G_MaterialListKey != G_MaterialListKeyDefault)
    {
        G_MaterialListKey = G_MaterialListKeyDefault;
        G_ActiveContainer = nullptr;
    }
}

namespace
{
    // One of FUN_004038f0's two quads (0x00403a92..0x00403c97 bulb, 0x00403c9a..0x00403e87
    // glow): nothing unless the horizontal radius is above 1 (TEST AH,0x41 / JNZ skips
    // not-greater and unordered); the colour (x, y, z) * 255 through ftol, nothing when the three
    // sum (unsigned) to 1 or less; the square centre +- radii clipped (FUN_00404670) to the
    // viewport's columns then rows with the container's UV rect; one record appended to the
    // container's current node: depth A, 1/-z, the clipped UVs, the colour 0xFFrrggbb (the
    // components' low bytes).
    void EnqueueFlareQuad(FScreenQuadBatchContainer* C, int const* Rect, float const* Color,
                          float RadiusX, float RadiusY, float ScreenX, float ScreenY,
                          float DepthA, float InvDepth)
    {
        if (!(RadiusX > 1.0f))
            return;
        unsigned int const R = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[0]) * static_cast<double>(255.0f)));
        unsigned int const G = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[1]) * static_cast<double>(255.0f)));
        unsigned int const B = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[2]) * static_cast<double>(255.0f)));
        if (B + G + R <= 1u)
            return;
        unsigned int const Packed = (((R | 0xffffff00u) << 8 | (G & 0xffu)) << 8) | (B & 0xffu);

        float X0 = static_cast<float>(static_cast<double>(ScreenX) - RadiusX);
        float X1 = static_cast<float>(static_cast<double>(ScreenX) + RadiusX);
        float Y0 = static_cast<float>(static_cast<double>(ScreenY) - RadiusY);
        float Y1 = static_cast<float>(static_cast<double>(ScreenY) + RadiusY);
        float U0 = BatchU0(C);
        float V0 = BatchV0(C);
        float U1 = BatchU1(C);
        float V1 = BatchV1(C);
        if (ScreenQuadClip1D(&X0, &X1, &U0, &U1, static_cast<float>(Rect[0]),
                             static_cast<float>(Rect[2]), RadiusX + RadiusX) == 0)
            return;
        if (ScreenQuadClip1D(&Y0, &Y1, &V0, &V1, static_cast<float>(Rect[1]),
                             static_cast<float>(Rect[3]), RadiusY + RadiusY) == 0)
            return;

        FScreenQuadRecord Rec;
        Rec.X0 = X0;
        Rec.Y0 = Y0;
        Rec.X1 = X1;
        Rec.Y1 = Y1;
        Rec.DepthA = DepthA;
        Rec.DepthB = InvDepth;
        Rec.U0 = U0;
        Rec.V0 = V0;
        Rec.U1 = U1;
        Rec.V1 = V1;
        Rec.ColorBits = Packed;
        AppendRecordToNode(C->Current, Rec);
    }
}

unsigned char ScreenQuadEnqueueFlare(ICamera* Cam, float const* Position, float const* BulbColor,
                                     float const* GlowColor, float BulbSize, float GlowSize)
{
    // FUN_004038f0 (cdecl), listing-traced 0x004038f0..0x00403e93. Sole caller FUN_004fdda0
    // (0x004fdfcc). No argument is null-checked in retail.

    // 0x004038f8..0x00403922: Position into camera space through the view transform (slot 16)
    // with DAT_00616494's TransformXform (slot 10).
    float const* const View = Cam->GetViewTransform();
    I3DMathEngine* const Math = AcquireScreenQuadMath();
    FVector CamSpace;
    Math->TransformXform(CamSpace, *reinterpret_cast<FTransform const*>(View),
                         *reinterpret_cast<FVector const*>(Position));

    // 0x00403925..0x0040396f: depth z - z * 0.001 (double DAT_005c75f8), stored as a float;
    // only in front of the near plane: depth - near below 0 (DAT_005c7474), or unordered
    // (TEST AH,1 / JNZ continues on C0).
    float const Depth = static_cast<float>(static_cast<double>(CamSpace.Z) -
                                           static_cast<double>(CamSpace.Z) * 0.001);
    if (static_cast<double>(Depth) - static_cast<double>(Cam->GetZNear()) >= 0.0)
        return 0;

    // 0x00403970..0x004039aa: InvDepth = -1 / depth (DAT_005c7500); the screen scales
    // near * x-scale-over-near (slot 11) and near * y-scale-over-near (slot 12), each through a
    // float temporary, in retail's call order (slot 11, 8, 12, 8).
    float const InvDepth = static_cast<float>(-1.0 / static_cast<double>(Depth));
    float const XScaleOverNear = static_cast<float>(Cam->GetXScaleOverNear());
    float const ScaleX =
        static_cast<float>(static_cast<double>(Cam->GetZNear()) * XScaleOverNear);
    float const YScaleOverNear = static_cast<float>(Cam->GetYScaleOverNear());
    float const ScaleY =
        static_cast<float>(static_cast<double>(Cam->GetZNear()) * YScaleOverNear);

    // 0x004039ae..0x004039e6: the radii. Horizontal ones keep their sign, vertical ones are
    // made positive: bulb_size * sx / -z and glow_size * sx / -z (and sy for the vertical).
    double const PerDepthX = static_cast<double>(ScaleX) * static_cast<double>(InvDepth);
    double const PerDepthY = static_cast<double>(ScaleY) * static_cast<double>(InvDepth);
    double const BulbRadiusXOnFpu = static_cast<double>(BulbSize) * PerDepthX;
    float const BulbRadiusX = static_cast<float>(BulbRadiusXOnFpu);
    float const BulbRadiusY =
        static_cast<float>(std::fabs(static_cast<double>(BulbSize) * PerDepthY));
    float const GlowRadiusX = static_cast<float>(PerDepthX * static_cast<double>(GlowSize));
    float const GlowRadiusY =
        static_cast<float>(std::fabs(PerDepthY * static_cast<double>(GlowSize)));

    // 0x004039ea..0x00403a0f: culled when both horizontal radii are below 1 (DAT_005c75dc;
    // TEST AH,5 / JP -- equal, greater and unordered continue). The bulb one is compared
    // unrounded, off the FPU.
    if (BulbRadiusXOnFpu < 1.0 && static_cast<double>(GlowRadiusX) < 1.0)
        return 0;

    // 0x00403a10..0x00403a78: the centre -- the viewport's (slot 5) half spans (DAT_005c75e0)
    // plus the scaled camera-space x / y, plus the viewport's origin.
    int const* const Rect = Cam->GetViewport();
    double const HalfSpanX = static_cast<double>(Rect[2] - Rect[0]) * static_cast<double>(0.5f);
    float const ScreenX = static_cast<float>(
        (HalfSpanX + static_cast<double>(CamSpace.X) * ScaleX * InvDepth) +
        static_cast<double>(Rect[0]));
    double const HalfSpanY = static_cast<double>(Rect[3] - Rect[1]) * static_cast<double>(0.5f);
    float const ScreenY = static_cast<float>(
        (HalfSpanY + static_cast<double>(CamSpace.Y) * ScaleY * InvDepth) +
        static_cast<double>(Rect[1]));

    // 0x00403a7c..0x00403a89: depth A = 1 - near * InvDepth.
    float const DepthA = static_cast<float>(
        1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));

    // 0x00403a8d: FUN_00402f40 -- the current container (EDI through both quads).
    FScreenQuadBatchContainer* const C = EnsureBatchContainer();

    // 0x00403a92..0x00403c97: the bulb quad with the bulb colour (argument 3);
    // 0x00403c9a..0x00403e87: the glow quad with the glow colour (argument 4).
    EnqueueFlareQuad(C, Rect, BulbColor, BulbRadiusX, BulbRadiusY, ScreenX, ScreenY, DepthA,
                     InvDepth);
    EnqueueFlareQuad(C, Rect, GlowColor, GlowRadiusX, GlowRadiusY, ScreenX, ScreenY, DepthA,
                     InvDepth);

    // 0x00403e8d: 1 on every path past the near and size culls.
    return 1;
}

void ScreenQuadBeginBatchFrame(unsigned long const* Frame, int BlendMode, bool bFind)
{
    // FUN_00402e30 = FUN_00402f40 (0x00402e34), then the body FUN_004045d0 has as a container
    // method (0x00402e39..0x00402ebd: key copy, current-node keep / drop / find, new node at
    // the live tail).
    ContainerBeginBatch(EnsureBatchContainer(), Frame, BlendMode, bFind);
}

void ScreenQuadAppendRecord(FScreenQuadRecord const& Record)
{
    // See the header: the node FUN_00402e30 returned is the container's current node.
    AppendRecordToNode(EnsureBatchContainer()->Current, Record);
}

void ScreenQuadEnqueueIcon(ICamera* Cam, float const* Position, unsigned long Argb, float Size,
                           float Angle, int /*Unused*/)
{
    // FUN_004034a0. 0x004034ab..0x004034d9: the projected centre and half extents (FUN_00403350
    // writes them into the stack and argument slots); nothing when culled.
    float ScreenX = 0.0f;    // local_2c
    float ScreenY = 0.0f;    // local_30
    float HalfW = 0.0f;      // param_2's slot
    float HalfH = 0.0f;      // param_1's slot
    float InvDepth = 0.0f;   // param_4's slot
    if (!ScreenQuadProjectObjectExtent(Cam, Position, Size, &ScreenX, &ScreenY, &HalfW, &HalfH,
                                       &InvDepth))
        return;
    FScreenQuadBatchContainer* const C = EnsureBatchContainer();   // FUN_00402f40, EBP
    int const* const Rect = Cam->GetViewport();                    // slot 5, EBX
    float const U0 = BatchU0(C);   // +0x20..+0x2c, copied as dwords
    float const V0 = BatchV0(C);
    float const U1 = BatchU1(C);
    float const V1 = BatchV1(C);

    // 0x004034ec..0x004034ff: |Angle| above 1e-5 (DAT_005c75f0; TEST AH,0x41 / JZ) rotates.
    if (!(std::fabs(static_cast<double>(Angle)) > 1.0e-5))
    {
        // 0x00403505..0x00403576: the square, each edge + 0.5 (DAT_005c75e8).
        float X0 = static_cast<float>(static_cast<double>(ScreenX) - HalfW + 0.5);
        float X1 = static_cast<float>(static_cast<double>(ScreenX) + HalfW + 0.5);
        float Y0 = static_cast<float>(static_cast<double>(ScreenY) - HalfH + 0.5);
        float Y1 = static_cast<float>(static_cast<double>(ScreenY) + HalfH + 0.5);
        float ClipU0 = U0;
        float ClipV0 = V0;
        float ClipU1 = U1;
        float ClipV1 = V1;

        // 0x0040357a..0x004035e0: clipped to the viewport's columns, then rows.
        if (ScreenQuadClip1D(&X0, &X1, &ClipU0, &ClipU1, static_cast<float>(Rect[0]),
                             static_cast<float>(Rect[2]), HalfW + HalfW) == 0)
            return;
        if (ScreenQuadClip1D(&Y0, &Y1, &ClipV0, &ClipV1, static_cast<float>(Rect[1]),
                             static_cast<float>(Rect[3]), HalfH + HalfH) == 0)
            return;

        // 0x004035e6..0x004036a8: depth 1 - near * InvDepth, one record appended to the current
        // node (grown to count + 0x21 records when full).
        float const DepthA = static_cast<float>(
            1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));
        FScreenQuadBatchNode* const Node = C->Current;
        int const Count = Node->RecordCount;
        Node->RecordCount = Count + 1;
        if (Count + 1 > Node->RecordCapacity)
        {
            Node->RecordCapacity = Count + 1 + 0x20;
            FScreenQuadRecord* const Grown = static_cast<FScreenQuadRecord*>(
                operator new(static_cast<size_t>(Node->RecordCapacity) * 0x2cu));
            if (Node->Records != nullptr)
                memcpy(Grown, Node->Records, static_cast<size_t>(Count) * 0x2cu);
            operator delete(Node->Records);   // FUN_005b7e1d
            Node->Records = Grown;
        }
        FScreenQuadRecord& Rec = Node->Records[Count];
        Rec.X0 = X0;
        Rec.Y0 = Y0;
        Rec.X1 = X1;
        Rec.Y1 = Y1;
        Rec.DepthA = DepthA;
        Rec.DepthB = InvDepth;
        Rec.U0 = ClipU0;
        Rec.V0 = ClipV0;
        Rec.U1 = ClipU1;
        Rec.V1 = ClipV1;
        Rec.ColorBits = static_cast<unsigned>(Argb);
        return;
    }

    // 0x004036b1..0x004036f7: the rotated half axes and the depth.
    double const Cos = std::cos(static_cast<double>(Angle));
    double const Sin = std::sin(static_cast<double>(Angle));
    float const CosHalfW = static_cast<float>(Cos * HalfW);
    float const SinHalfW = static_cast<float>(static_cast<double>(HalfW) * Sin);
    float const CosHalfH = static_cast<float>(Cos * HalfH);
    float const SinHalfH = static_cast<float>(Sin * HalfH);
    float const DepthA = static_cast<float>(
        1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));

    // 0x004036fb..0x0040376b: the corners, in retail's operand order.
    double const X = static_cast<double>(ScreenX);
    double const Y = static_cast<double>(ScreenY);
    double const XMinusCos = X - CosHalfW;
    double const YMinusSin = Y - SinHalfW;
    float CornerX[4];
    float CornerY[4];
    CornerX[0] = static_cast<float>(XMinusCos - SinHalfH);
    CornerY[0] = static_cast<float>((static_cast<double>(SinHalfW) + Y) - CosHalfH);
    CornerX[1] = static_cast<float>(XMinusCos + SinHalfH);
    CornerY[1] = static_cast<float>((static_cast<double>(CosHalfH) + SinHalfW) + Y);
    CornerX[2] = static_cast<float>((static_cast<double>(SinHalfH) + CosHalfW) + X);
    CornerY[2] = static_cast<float>(static_cast<double>(CosHalfH) + YMinusSin);
    CornerX[3] = static_cast<float>((static_cast<double>(CosHalfW) + X) - SinHalfH);
    CornerY[3] = static_cast<float>(YMinusSin - CosHalfH);

    // 0x0040376f..0x004037c4: every corner inside [left, right) x [top, bottom), unordered
    // values passing (TEST AH,5 / JNP; TEST AH,0x41 / JNP, JZ, JNP).
    float const Left = static_cast<float>(Rect[0]);
    for (int Index = 0; Index < 4; ++Index)
    {
        if (CornerX[Index] < Left)
            return;
        if (static_cast<double>(Rect[2]) <= static_cast<double>(CornerX[Index]))
            return;
        if (static_cast<double>(Rect[1]) > static_cast<double>(CornerY[Index]))
            return;
        if (static_cast<double>(Rect[3]) <= static_cast<double>(CornerY[Index]))
            return;
    }

    // 0x004037c6..0x004038e3: four vertices appended (grown to count + 0x24 when full):
    // (u0, v0), (u0, v1), (u1, v1), (u1, v0).
    FScreenQuadBatchNode* const Node = C->Current;
    int const Count = Node->RotVertCount;
    Node->RotVertCount = Count + 4;
    if (Count + 4 > Node->RotVertCapacity)
    {
        Node->RotVertCapacity = Count + 4 + 0x20;
        FScreenQuadGpuVert* const Grown = static_cast<FScreenQuadGpuVert*>(
            operator new(static_cast<size_t>(Node->RotVertCapacity) * 0x20u));
        if (Node->RotatedVerts != nullptr)
            memcpy(Grown, Node->RotatedVerts, static_cast<size_t>(Count) * 0x20u);
        operator delete(Node->RotatedVerts);   // FUN_005b7e1d
        Node->RotatedVerts = Grown;
    }
    float const VertU[4] = {U0, U0, U1, U1};
    float const VertV[4] = {V0, V1, V1, V0};
    for (int Index = 0; Index < 4; ++Index)
    {
        FScreenQuadGpuVert& Vert = Node->RotatedVerts[Count + Index];
        Vert.X = CornerX[Index];
        Vert.Y = CornerY[Index];
        Vert.Z = DepthA;
        Vert.Rhw = InvDepth;
        Vert.Color = static_cast<unsigned>(Argb);
        Vert.Specular = 0;
        Vert.U = VertU[Index];
        Vert.V = VertV[Index];
    }
}

// DAT_0061648c -- the static initializer LAB_00402c00 stores -1 (XREFs: 00402c00 W, 00404045 R,
// 00404b80 R -- never written again).
unsigned long G_ScreenQuadCurrentListKey = 0xffffffffu;

// FUN_00403ea0 (cdecl: pipeline, list key), listing-traced 0x00403ea0..0x004042bf.
void ScreenQuadFlush(IRenderPipeline* Pipe, unsigned long Key)
{
    // 0x00403ec6..0x00403f69: three savers on the caller's pipeline (slot 61 read, slot 60
    // write): 0x88 -> 0, 0x8b -> -1, 0x16 -> 1. They restore in reverse order at the end
    // (0x00404267..0x004042a6: 0x16 inline, then FUN_00405840 for 0x8b and 0x88).
    FScreenQuadRenderStateSaver const SaverA(Pipe, 0x88, 0);
    FScreenQuadRenderStateSaver const SaverB(Pipe, 0x8b, -1);
    FScreenQuadRenderStateSaver const SaverC(Pipe, 0x16, 1);

    // 0x00403f78..0x00403f98: DAT_005c7520 {0x89,0} {0x1d,0} {0x1b,0}.
    Pipe->SetRenderState(D3DRS_LIGHTING, 0);
    Pipe->SetRenderState(D3DRS_SPECULARENABLE, 0);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    // 0x00403f9a..0x00403fba: DAT_005c7540 {0x8d,1} {0x91,1}.
    Pipe->SetRenderState(D3DRS_COLORVERTEX, 1);
    Pipe->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, 1);
    // 0x00403fbc..0x00403fc3: stage 0 type 0xb (the table loop leaves EAX = 0) = 0.
    Pipe->SetTextureStageState(0, 0xb, 0);
    // 0x00403fc9..0x00403feb: DAT_005c7568 on stage 0.
    Pipe->SetTextureStageState(0, 1, 4);
    Pipe->SetTextureStageState(0, 2, 2);
    Pipe->SetTextureStageState(0, 3, 0);
    Pipe->SetTextureStageState(0, 4, 4);
    Pipe->SetTextureStageState(0, 5, 2);
    Pipe->SetTextureStageState(0, 6, 0);
    // 0x00403fed..0x0040400f: DAT_005c75a0 on stage 1.
    Pipe->SetTextureStageState(1, 1, 1);
    Pipe->SetTextureStageState(1, 2, 2);
    Pipe->SetTextureStageState(1, 3, 1);
    Pipe->SetTextureStageState(1, 4, 1);
    Pipe->SetTextureStageState(1, 5, 2);
    Pipe->SetTextureStageState(1, 6, 1);
    // 0x00404011..0x00404038: stage 0 types 0x18 = 0, 0xd = 3, 0xe = 3.
    Pipe->SetTextureStageState(0, 0x18, 0);
    Pipe->SetTextureStageState(0, 0xd, 3);
    Pipe->SetTextureStageState(0, 0xe, 3);

    if (Key == G_ScreenQuadCurrentListKey)
    {
        // 0x00404051..0x004040d9: the current container, created on demand -- the pair
        // {DAT_00616474, FUN_004045b0 temporary} inserted (FUN_00404f00 / FUN_00404e40, the
        // temporaries destroyed empty by FUN_00403200); a new entry is given the default frame
        // through FUN_004045d0(DAT_006101d8, 0, 1).
        if (G_ActiveContainer == nullptr)
        {
            std::pair<std::map<unsigned long, FScreenQuadBatchEntry>::iterator, bool> const
                Inserted = G_BatchMap.try_emplace(G_MaterialListKey);
            G_ActiveContainer = &Inserted.first->second.Container;
            if (Inserted.second)
                ContainerBeginBatch(G_ActiveContainer, G_ScreenQuadDefaultFrame, 0, true);
        }
        // 0x004040df..0x0040415a.
        FlushContainer(G_ActiveContainer);
    }
    else
    {
        // 0x0040415f..0x00404200: FUN_00432240 (find); the end node -> nothing drawn.
        std::map<unsigned long, FScreenQuadBatchEntry>::iterator const Found =
            G_BatchMap.find(Key);
        if (Found != G_BatchMap.end())
            FlushContainer(&Found->second.Container);
    }

    // 0x00404207..0x00404227: DAT_005c7558 {0x8d,0}. 0x00404229..0x00404241: stage 0 types
    // 0xd and 0xe = 1.
    Pipe->SetRenderState(D3DRS_COLORVERTEX, 0);
    Pipe->SetTextureStageState(0, 0xd, 1);
    Pipe->SetTextureStageState(0, 0xe, 1);

    // 0x00404247..0x0040425d: the default key back; DAT_00616498 nulled only on a change.
    if (G_MaterialListKey != G_MaterialListKeyDefault)
    {
        G_MaterialListKey = G_MaterialListKeyDefault;
        G_ActiveContainer = nullptr;
    }

    // Port bookkeeping, not retail: every state above went to the pipeline directly, so the
    // ScreenQuadSet* cache the menu path keeps (see the header) no longer mirrors the device.
    // Runs before the savers' destructors, which also write directly.
    ScreenQuadInvalidateStateCache();
}

// DAT_006101ec -- .data byte, initial 1, never written in the PE; sole reader FUN_00404720
// (0x00404951). Set: the big-image tiles are drawn with stage 0 MAGFILTER (0x10) and MINFILTER
// (0x11) forced to 1 (point).
unsigned char G_BigImageTilePointFilter = 1;

void __cdecl ScreenQuadDrawBigImageTile(void* TextureHandle, unsigned long PackedColor,
                                        float* DestXy, float* UvQuad)
{
    // FUN_00404720 (cdecl, RET), listing-traced 0x00404720..0x00404cdb. No argument is checked.
    // 0x0040473a..0x00404744: glMatrixMode(0x1701) is the empty RET at 0x00401e90.
    // 0x00404747 / 0x0040474c: FUN_00422a70, FUN_004225e0.
    Win3DPushProjection();
    Win3DSetOrtho2D();

    // 0x00404751..0x0040480f: three savers on DALib::Pipeline (slot 61 read, slot 60 write; EH
    // states 0..2): 0x88 -> 0, 0x8b -> -1, 0x16 -> 1.
    FScreenQuadRenderStateSaver const SaverA(DALib::Pipeline, 0x88, 0);
    FScreenQuadRenderStateSaver const SaverB(DALib::Pipeline, 0x8b, -1);
    FScreenQuadRenderStateSaver const SaverC(DALib::Pipeline, 0x16, 1);

    // 0x0040480f..0x0040483b: DAT_005c7520 {0x89,0} {0x1d,0} {0x1b,0}.
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    DALib::Pipeline->SetRenderState(D3DRS_SPECULARENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    // 0x0040483d..0x00404866: DAT_005c7540 {0x8d,1} {0x91,1}.
    DALib::Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);
    DALib::Pipeline->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, 1);
    // 0x00404868..0x0040487e: stage 0 type 0xb = 0.
    DALib::Pipeline->SetTextureStageState(0, 0xb, 0);
    // 0x0040487e..0x004048a8: DAT_005c7568 on stage 0.
    DALib::Pipeline->SetTextureStageState(0, 1, 4);
    DALib::Pipeline->SetTextureStageState(0, 2, 2);
    DALib::Pipeline->SetTextureStageState(0, 3, 0);
    DALib::Pipeline->SetTextureStageState(0, 4, 4);
    DALib::Pipeline->SetTextureStageState(0, 5, 2);
    DALib::Pipeline->SetTextureStageState(0, 6, 0);
    // 0x004048aa..0x004048d8: DAT_005c75a0 on stage 1.
    DALib::Pipeline->SetTextureStageState(1, 1, 1);
    DALib::Pipeline->SetTextureStageState(1, 2, 2);
    DALib::Pipeline->SetTextureStageState(1, 3, 1);
    DALib::Pipeline->SetTextureStageState(1, 4, 1);
    DALib::Pipeline->SetTextureStageState(1, 5, 2);
    DALib::Pipeline->SetTextureStageState(1, 6, 1);
    // 0x004048da..0x0040491e: stage 0 types 0x18 = 0, 0xd = 3, 0xe = 3.
    DALib::Pipeline->SetTextureStageState(0, 0x18, 0);
    DALib::Pipeline->SetTextureStageState(0, 0xd, 3);
    DALib::Pipeline->SetTextureStageState(0, 0xe, 3);

    // 0x0040491e..0x00404951: stage 0 types 0x10 and 0x11 read (slot 63) into two locals.
    int SavedStage0Type10 = 0;   // [ESP+0x8]
    int SavedStage0Type11 = 0;   // [ESP+0xc]
    DALib::Pipeline->GetTextureStageState(0, 0x10, &SavedStage0Type10);
    DALib::Pipeline->GetTextureStageState(0, 0x11, &SavedStage0Type11);

    // 0x00404951..0x00404987: DAT_006101ec set (TEST AL,AL / JZ skips) -> both = 1.
    if (G_BigImageTilePointFilter != 0)
    {
        DALib::Pipeline->SetTextureStageState(0, 0x10, 1);
        DALib::Pipeline->SetTextureStageState(0, 0x11, 1);
    }

    // 0x00404987..0x004049fe: two more savers (EH states 3, 4): 0x17 -> 8, 0xe -> 0.
    FScreenQuadRenderStateSaver const SaverD(DALib::Pipeline, 0x17, 8);
    FScreenQuadRenderStateSaver const SaverE(DALib::Pipeline, 0xe, 0);

    // 0x004049fe..0x00404a2a: FUN_00402d30 inlined on DACOM_CRC::GetCRC32("image").
    ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("image"));

    // 0x00404a2a..0x00404a53: the frame {handle, UvQuad[0], UvQuad[7], UvQuad[6], UvQuad[1]}
    // (u0, v0, u1, v1), copied as dwords.
    unsigned long Frame[5];
    Frame[0] = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(TextureHandle));
    memcpy(&Frame[1], &UvQuad[0], sizeof(unsigned long));
    memcpy(&Frame[2], &UvQuad[7], sizeof(unsigned long));
    memcpy(&Frame[3], &UvQuad[6], sizeof(unsigned long));
    memcpy(&Frame[4], &UvQuad[1], sizeof(unsigned long));

    // 0x00404a57..0x00404a73: FUN_00402f40, then FUN_004045d0(frame, blend 2, bFind 0) -> ESI.
    FScreenQuadBatchNode* const Node =
        ContainerBeginBatch(EnsureBatchContainer(), Frame, 2, false);

    // 0x00404a6c..0x00404a94: the destination rect into locals.
    float X0 = DestXy[0];   // [ESP+0x4c]
    float Y0 = DestXy[1];   // [ESP+0x50]
    float X1 = DestXy[2];   // [ESP+0x54]
    float Y1 = DestXy[3];   // [ESP+0x58]
    float U0 = 0.0f;        // [ESP+0x60]
    float V0 = 0.0f;        // [ESP+0x64]
    float U1 = 0.0f;        // [ESP+0x68]
    float V1 = 0.0f;        // [ESP+0x6c]
    memcpy(&U0, &Frame[1], sizeof(float));
    memcpy(&V0, &Frame[2], sizeof(float));
    memcpy(&U1, &Frame[3], sizeof(float));
    memcpy(&V1, &Frame[4], sizeof(float));

    // 0x00404a88..0x00404ad7: FUN_00404670 on x against [0, DAT_00610850 - 1] (FILD of the
    // decremented int), span X1 - X0 stored as a float; 0 skips to 0x00404b97.
    float const SpanX = static_cast<float>(static_cast<double>(X1) - static_cast<double>(X0));
    float const HiX = static_cast<float>(static_cast<int>(G_RenderSurfaceWidth - 1u));
    if (ScreenQuadClip1D(&X0, &X1, &U0, &U1, 0.0f, HiX, SpanX) != 0)
    {
        // 0x00404add..0x00404b24: the same on y against [0, DAT_00610854 - 1].
        float const SpanY = static_cast<float>(static_cast<double>(Y1) - static_cast<double>(Y0));
        float const HiY = static_cast<float>(static_cast<int>(G_RenderSurfaceHeight - 1u));
        if (ScreenQuadClip1D(&Y0, &Y1, &V0, &V1, 0.0f, HiY, SpanY) != 0)
        {
            // 0x00404b26..0x00404b7d: FUN_00404ce0(node + 8, 1) and the record written into the
            // slot it returns: the clipped rect, 1.0 / 1.0, the clipped UVs, the colour dword.
            FScreenQuadRecord Rec;
            Rec.X0 = X0;
            Rec.Y0 = Y0;
            Rec.X1 = X1;
            Rec.Y1 = Y1;
            Rec.DepthA = 1.0f;
            Rec.DepthB = 1.0f;
            Rec.U0 = U0;
            Rec.V0 = V0;
            Rec.U1 = U1;
            Rec.V1 = V1;
            Rec.ColorBits = static_cast<unsigned>(PackedColor);
            AppendRecordToNode(Node, Rec);

            // 0x00404b80..0x00404b94: FUN_00403ea0(DALib::Pipeline, DAT_0061648c).
            ScreenQuadFlush(DALib::Pipeline, G_ScreenQuadCurrentListKey);
        }
    }

    // 0x00404b97..0x00404bc6: DAT_005c7558 {0x8d,0}. 0x00404bc8..0x00404bf5: stage 0 types 0xd
    // and 0xe = 1. 0x00404bf5..0x00404c28: stage 0 types 0x10 / 0x11 back to the saved values.
    DALib::Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    DALib::Pipeline->SetTextureStageState(0, 0xd, 1);
    DALib::Pipeline->SetTextureStageState(0, 0xe, 1);
    DALib::Pipeline->SetTextureStageState(0, 0x10, SavedStage0Type10);
    DALib::Pipeline->SetTextureStageState(0, 0x11, SavedStage0Type11);

    // 0x00404c28: FUN_00422ab0. The savers then restore in reverse order (0x00404c2d..0x00404cc5:
    // 0xe, 0x17, 0x16, 0x8b, 0x88).
    Win3DPopProjection();
}

unsigned char ScreenQuadProjectObjectExtent(ICamera* Cam, float const* Position, float Size,
                                            float* OutScreenX, float* OutScreenY,
                                            float* OutHalfW, float* OutHalfH,
                                            float* OutInvDepth)
{
    // FUN_00403350. 0x00403353..0x00403382: Position into camera space through the view
    // transform (slot 16) with DAT_00616494's TransformXform (slot 10).
    float const* const View = Cam->GetViewTransform();
    I3DMathEngine* const Math = AcquireScreenQuadMath();
    FVector CamSpace;
    Math->TransformXform(CamSpace, *reinterpret_cast<FTransform const*>(View),
                         *reinterpret_cast<FVector const*>(Position));

    // 0x00403385..0x0040339a: only in front of the near plane (z - near below 0, or unordered;
    // TEST AH,1 / JNZ).
    if (static_cast<double>(CamSpace.Z) - static_cast<double>(Cam->GetZNear()) >= 0.0)
        return 0;

    // 0x004033a4..0x004033ec: InvDepth = -1 / z (DAT_005c7500), stored as a float; the two
    // screen scales near * x-scale-over-near (slot 11) and near * y-scale-over-near (slot 12),
    // each stored as a float.
    float const InvDepth = static_cast<float>(-1.0 / static_cast<double>(CamSpace.Z));
    *OutInvDepth = InvDepth;
    float const XScaleOverNear = static_cast<float>(Cam->GetXScaleOverNear());
    float const ScaleX = static_cast<float>(static_cast<double>(Cam->GetZNear()) * XScaleOverNear);
    float const YScaleOverNear = static_cast<float>(Cam->GetYScaleOverNear());
    float const ScaleY = static_cast<float>(static_cast<double>(Cam->GetZNear()) * YScaleOverNear);

    // 0x004033f0..0x00403427: the half extents (Size / 2, DAT_005c75e0), the vertical one made
    // positive; culled when the horizontal one (unrounded) is below 1 (TEST AH,5 / JP).
    double const HalfW = static_cast<double>(ScaleX) * static_cast<double>(InvDepth) *
                         static_cast<double>(Size) * static_cast<double>(0.5f);
    *OutHalfW = static_cast<float>(HalfW);
    *OutHalfH = static_cast<float>(std::fabs(static_cast<double>(ScaleY) *
                                             static_cast<double>(InvDepth) *
                                             static_cast<double>(Size) *
                                             static_cast<double>(0.5f)));
    if (HalfW < 1.0)
        return 0;

    // 0x00403431..0x00403497: the centre -- the viewport's (slot 5) half spans plus its origin,
    // offset by the scaled camera-space x / y.
    int const* const Rect = Cam->GetViewport();
    double const HalfSpanY = static_cast<double>(Rect[3] - Rect[1]) * static_cast<double>(0.5f);
    double const HalfSpanX = static_cast<double>(Rect[2] - Rect[0]) * static_cast<double>(0.5f);
    *OutScreenX = static_cast<float>(
        (HalfSpanX + static_cast<double>(CamSpace.X) * ScaleX * InvDepth) +
        static_cast<double>(Rect[0]));
    *OutScreenY = static_cast<float>(
        (static_cast<double>(CamSpace.Y) * ScaleY * InvDepth + static_cast<double>(Rect[1])) +
        HalfSpanY);
    return 1;
}
