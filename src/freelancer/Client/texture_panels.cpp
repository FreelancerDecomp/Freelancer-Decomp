// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TexturePanels / planet-ring — PTR_LAB_005db1b4. Epic: texture-panels.
// Listing: freelancer.exe.txt LAB/FUN_0050a040…0050bfa0, FUN_00551500, LAB_005091a0.

#include "texture_panels.h"

#include "DALib.h"
#include "asteroid_billboard.h" // FBillboardVertexCursor ({pointer, stride}, dtor FUN_00552b20)
#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_universe.h"
#include "effect_vertex_batch.h" // EffectVertexBatchDestroy (FUN_0046e9c0), the atexit LAB_0050a020
#include "engine_light.h"
#include "part_cache.h"    // FvfTexcoordBlockBytes (FUN_004061a0)
#include "perf_options.h"
#include "pixel_format.h"  // PixelFormatFromD3D / PixelFormatGetDesc (FUN_005b7010 / 005b7030)
#include "render/RenderPipeline.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "star_sphere.h"
#include "star_sys.h"
#include "win3d_immediate.h"
#include "x86math.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

namespace
{
    float const kColorScale = 1.0f / 255.0f; // DAT_005c7f68 = 0x3b808081
    float const kZero = 0.0f;                // DAT_005c7474

    // DAT_00675060 / DAT_00675064 — global TexturePanels instance list (Create).
    FTexturePanelsGlobalNode G_PanelsListSentinel = {&G_PanelsListSentinel, &G_PanelsListSentinel,
                                                     nullptr};
    int G_PanelsListCount = 0;

    // DAT_00613324 — the panels detail level FUN_00509f60 stores (.data initial 1.0f).
    float G_PanelsDetail = 1.0f;

    // FUN_00554e90 — StarSys active-system id thunk (DAT_00679954).
    unsigned QueryActiveSystemIdStarSys()
    {
        return StarSysGetActiveSystemId();
    }

    // The {state, value} tables below are copied dword for dword from the retail .rdata,
    // terminator included (the walks stop at a zero state).
    // DAT_005db0d8 -- RS: LIGHTING (0x89) 0, SPECULARENABLE (0x1d) 0, ALPHABLENDENABLE (0x1b) 1,
    // SRCBLEND (0x13) SRCALPHA, DESTBLEND (0x14) INVSRCALPHA.
    unsigned long const kRsPanelBoth[] = {0x89, 0, 0x1d, 0, 0x1b, 1, 0x13, 5, 0x14, 6, 0};
    // DAT_005db108 stage0 / DAT_005db140 stage1 / DAT_005db178 stage2 -- TSS COLOROP, COLORARG1,
    // COLORARG2, ALPHAOP, ALPHAARG1, ALPHAARG2.
    unsigned long const kTssStage0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    unsigned long const kTssStage1[] = {1, 5, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0};
    unsigned long const kTssStage2[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    // DAT_005e09c8 used by FUN_00551500
    unsigned long const kRsCloudsDraw[] = {0x8d, 1, 0x91, 1, 0};
    // DAT_005e09e0 -- restore after DrawClouds: only 0x8d (the table is {0x8d, 0, 0}).
    unsigned long const kRsCloudsRestore[] = {0x8d, 0, 0};
    // DAT_005e09f0 / DAT_005e0a28 -- TSS stage0/1 tables used by FUN_00551500
    unsigned long const kTssClouds0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    unsigned long const kTssClouds1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    // FUN_00552420 (thiscall on the format id) — 0x00552420..0x0055243b: ids 10..14 (CMP 0xa JL /
    // CMP 0xe JG) have alpha; any other id returns its descriptor's +0x48 byte (FUN_005b7030).
    unsigned char TextureFormatHasAlpha(int FormatId)
    {
        if (FormatId >= 10 && FormatId <= 14)
            return 1;
        return static_cast<unsigned char>(PixelFormatGetDesc(FormatId)->HasAlpha & 0xffu);
    }

    // FUN_00552440 — XYZ/XYZRHW size from FVF low byte.
    int FvfPositionBytes(unsigned char FvfLow)
    {
        if ((FvfLow & 0xc) == 0xc)
            return 0x1c;
        if ((FvfLow & 4) == 4)
            return 0x10;
        if ((FvfLow & 2) == 2)
            return 0xc;
        return 0;
    }

    // FUN_00552480 — diffuse (+normal) size from FVF low byte.
    int FvfDiffuseNormalBytes(unsigned char FvfLow)
    {
        int Bytes = 0;
        if ((FvfLow & 0x40) != 0)
            Bytes = 4;
        if (static_cast<char>(FvfLow) < 0)
            Bytes += 4;
        return Bytes;
    }

    // FUN_004d7270 (thiscall, RET 4), 0x004d7270..0x004d728e -- this[i] = other[i] + this[i]
    // for i = 0..2 (FLD other, FADD this, FSTP this); returns this (MOV EAX,ECX).
    float* Vec3AddInPlace(float* This, float const* Other)
    {
        This[0] = Other[0] + This[0];
        This[1] = Other[1] + This[1];
        This[2] = Other[2] + This[2];
        return This;
    }

    // DAT_00675018 — TexturePanels piece vertex batcher (embedded object, not a pointer).
    // Layout recovered from FUN_0050c190 / 0050c120 / 0050bb60 / DrawPiece stores.
    struct FPanelVert20
    {
        float X, Y, Z;           // +0
        unsigned char B, G, R, A; // +0xc
        float U, V;              // +0x10
        float U1, V1;            // +0x18
    };
    static_assert(sizeof(FPanelVert20) == 0x20, "panel batcher vert 0x20");

    // FUN_00405840's object: a render-state saver {Pipeline, State, Old}. Retail builds it in
    // place (MOV [ESP+..],Pipeline / State, then vt+0xf4 GetRenderState(State, &Old)) and the
    // destructor FUN_00405840 puts Old back through vt+0xf0 SetRenderState.
    struct FRenderStateRestorer
    {
        explicit FRenderStateRestorer(int InState);
        ~FRenderStateRestorer() noexcept(false);

        IRenderPipeline* Pipeline; // +0
        int State;                   // +4
        int Old;                     // +8
    };

    void RenderStateSave(FRenderStateRestorer* Saver, int State)
    {
        Saver->Pipeline = DALib::Pipeline;
        Saver->State = State;
        Saver->Pipeline->GetRenderState((D3DRENDERSTATETYPE)State, &Saver->Old);
    }

    // FUN_00405840: SetRenderState([+4], [+8]) on [+0].
    void RenderStateRestore(FRenderStateRestorer* Saver)
    {
        Saver->Pipeline->SetRenderState((D3DRENDERSTATETYPE)Saver->State, Saver->Old);
    }

    FRenderStateRestorer::FRenderStateRestorer(int InState)
    {
        RenderStateSave(this, InState);
    }

    FRenderStateRestorer::~FRenderStateRestorer() noexcept(false)
    {
        RenderStateRestore(this);
    }

    struct FPanelVertexBatcher
    {
        unsigned long Field00;     // +0x00  primitive type: ctor 1, DrawPiece writes 5
        FPanelVert20* Verts;       // +0x04
        int VertCount;             // +0x08
        unsigned long Fvf;         // +0x0c  ctor 0x242 (XYZ|DIFFUSE|TEX2, 0x20-byte vertex)
        unsigned short* Indices;   // +0x10
        int IndexCount;            // +0x14
        // +0x18 DAT_00675030: the pipeline the batch draws through, AddRef'd while held
        // (DrawPiece 0050b6e8..0050b712 swaps it in, 0050ba78..0050ba87 releases it).
        IRenderPipeline* Pipeline;
        int VertGrow;              // +0x1c
        unsigned VertCap;          // +0x20
        unsigned IndexCap;         // +0x24
        unsigned char Flag28;      // +0x28
        unsigned char Flag29;      // +0x29
        unsigned char Pad2A[2];
        int QuadCounter;           // +0x2c
        // DrawPiece 0050b754..0050b780 stores ftol(This+0x44 R / +0x48 G / +0x4c B / +0x50 A * 255)
        // into +0x30 / +0x31 / +0x32 / +0x33; PushVert 0050bbd2.. copies +0x30 to vertex +0xe (R),
        // +0x31 to +0xd (G), +0x32 to +0xc (B), +0x33 to +0xf (A).
        unsigned char ColorR;      // +0x30
        unsigned char ColorG;      // +0x31
        unsigned char ColorB;      // +0x32
        unsigned char ColorA;      // +0x33
        float StageU;              // +0x34
        float StageV;              // +0x38
        float StageU1;             // +0x3c
        float StageV1;             // +0x40
    };

    // Static initializer LAB_00509f90: FUN_00509fb0(this = DAT_00675018, 0, 0x80) — +0x00 = 1,
    // +0x04/+0x08/+0x10/+0x14 = 0, +0x0c = 0x242, +0x18 = param_1 (0; AddRef skipped), colour
    // bytes +0x30..+0x33 = 0xff, +0x20/+0x24/+0x28/+0x29/+0x2c/+0x34..+0x40 = 0,
    // +0x1c VertGrow = param_2 (0x80). Retail then registers LAB_0050a020 with atexit
    // (0x00509fa1..0x00509fa6, FUN_005b7e6c) -- G_PanelBatcherAtExit below.
    FPanelVertexBatcher G_PanelBatcher = {
        1u, nullptr, 0, 0x242u, nullptr, 0, nullptr, 0x80, 0u, 0u, 0, 0, {0, 0}, 0,
        0xffu, 0xffu, 0xffu, 0xffu, 0.0f, 0.0f, 0.0f, 0.0f};

    // The destructor retail runs on this batch is the effect batch's FUN_0046e9c0 (one body for
    // both under /OPT:ICF): it touches only +0x04, +0x10 and +0x18, which sit at the same offsets
    // in both port types.
    static_assert(offsetof(FPanelVertexBatcher, Verts) == offsetof(FEffectVertexBatch, Vertices),
                  "FUN_0046e9c0 frees +0x04");
    static_assert(offsetof(FPanelVertexBatcher, Indices) == offsetof(FEffectVertexBatch, Indices),
                  "FUN_0046e9c0 frees +0x10");
    static_assert(offsetof(FPanelVertexBatcher, Pipeline) == offsetof(FEffectVertexBatch, Pipeline),
                  "FUN_0046e9c0 releases +0x18");

    // DAT_00675010 -- LAB_0050a020's once-flag.
    unsigned char G_PanelBatcherDtorOnce = 0;

    // LAB_0050a020 (the atexit entry): bit 0 of DAT_00675010 tested and set, then JMP FUN_0046e9c0
    // with ECX = DAT_00675018 (0x0050a034..0x0050a039). A file-scope object's destructor is the
    // port's atexit registration, as for the effect batch (effect_vertex_batch.cpp).
    struct FPanelBatcherAtExit
    {
        ~FPanelBatcherAtExit()
        {
            if ((G_PanelBatcherDtorOnce & 1u) != 0u)
                return;
            G_PanelBatcherDtorOnce = static_cast<unsigned char>(G_PanelBatcherDtorOnce | 1u);
            EffectVertexBatchDestroy(reinterpret_cast<FEffectVertexBatch*>(&G_PanelBatcher));
        }
    };

    FPanelBatcherAtExit G_PanelBatcherAtExit;

    // FUN_0050c120 — grow vertex buffer when VertCount >= VertCap.
    void PanelBatcherGrowVerts(FPanelVertexBatcher* B)
    {
        if (B->VertCap > static_cast<unsigned>(B->VertCount))
            return;
        int const Grow = B->VertGrow;
        unsigned const NewCap = B->VertCap + static_cast<unsigned>(Grow);
        FPanelVert20* const Neu =
            static_cast<FPanelVert20*>(operator new(NewCap * sizeof(FPanelVert20)));
        if (Neu == nullptr)
            return;
        if (B->Verts != nullptr)
        {
            memcpy(Neu, B->Verts, B->VertCap * sizeof(FPanelVert20));
            operator delete(B->Verts);
        }
        B->Verts = Neu;
        B->VertCap = NewCap;
    }

    // FUN_0050c190 — reset counters and ensure index capacity for one more slot.
    void PanelBatcherReset(FPanelVertexBatcher* B)
    {
        B->VertCount = 0;
        B->Flag29 = 0;
        B->Flag28 = 0;
        B->QuadCounter = 0;
        B->IndexCount = 0;
        PanelBatcherGrowVerts(B);
        if (B->IndexCap < static_cast<unsigned>(B->IndexCount) + 1u)
        {
            int const Grow = B->VertGrow * 3;
            unsigned const NewCap = B->IndexCap + static_cast<unsigned>(Grow);
            unsigned short* const Neu =
                static_cast<unsigned short*>(operator new(NewCap * 2u));
            if (Neu != nullptr)
            {
                if (B->Indices != nullptr)
                {
                    memcpy(Neu, B->Indices, B->IndexCap * 2u);
                    operator delete(B->Indices);
                }
                B->Indices = Neu;
                B->IndexCap = NewCap;
            }
        }
    }

    // FUN_0050bb60 — push one XYZ vertex from staging color/UV.
    void PanelBatcherPushVert(FPanelVertexBatcher* B, float X, float Y, float Z)
    {
        if (B->Flag29 != 0)
        {
            if ((B->QuadCounter & 3) == 3)
            {
                B->VertCount += 2;
                PanelBatcherGrowVerts(B);
                int const C = B->VertCount;
                memcpy(&B->Verts[C - 2], &B->Verts[C - 5], sizeof(FPanelVert20));
                memcpy(&B->Verts[C - 1], &B->Verts[C - 3], sizeof(FPanelVert20));
            }
            B->QuadCounter += 1;
        }
        FPanelVert20* const V = &B->Verts[B->VertCount];
        V->B = B->ColorB;
        V->G = B->ColorG;
        V->R = B->ColorR;
        V->A = B->ColorA;
        V->U = B->StageU;
        V->V = B->StageV;
        V->U1 = B->StageU1;
        V->V1 = B->StageV1;
        V->X = X;
        V->Y = Y;
        V->Z = Z;
        B->VertCount += 1;
        PanelBatcherGrowVerts(B);
    }

    // FUN_0050bb20 (cdecl: pipeline, stage, table), listing-traced 0x0050bb20..0x0050bb51 --
    // pipeline slot 62 (+0xf8, SetTextureStageState) on each {type, value} pair until a 0 type.
    // Neither the pipeline nor the table is tested.
    void PipelineApplyTssPairTable(IRenderPipeline* Pipe, unsigned long Stage,
                                   unsigned long const* Table)
    {
        unsigned long const* Cursor = Table;
        unsigned long State = Cursor[0];
        while (State != 0)
        {
            Pipe->SetTextureStageState(static_cast<int>(Stage), static_cast<int>(State),
                                       static_cast<int>(Cursor[1]));
            State = Cursor[2];
            Cursor += 2;
        }
    }

    // FUN_0050b2d0 (__thiscall, returns AL) -- bind the ring's textures and states.
    //   detail shape (+0x58) and shape (+0x54): stage 0 = shape frame 0 (slot 66), TSS 0 0xb = 0;
    //     stage 1 = detail frame 0, TSS 1 0xb = 1; tables DAT_005db108 / 005db140 / 005db178 on
    //     stages 0 / 1 / 2, RS DAT_005db0d8, then VerifyState (slot 68, result dropped); 1.
    //   shape only: stage 0 as above, DAT_005db108 on stage 0, DAT_005db178 on stage 1, RS
    //     DAT_005db0d8, no VerifyState; 1.
    //   otherwise 0.
    // FUN_00425640 returns null for a reference that holds no record, so the handles need no
    // separate test.
    int SetupPanelPipeline(FTexturePanels* This)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (This->DetailShape != nullptr && This->Shape != nullptr)
        {
            Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(This->Shape, 0));
            Pipe->SetTextureStageState(0, 0xb, 0);
            Pipe->SetTextureStageTexture(1, TexShapeGetGpuHandle(This->DetailShape, 0));
            Pipe->SetTextureStageState(1, 0xb, 1);
            PipelineApplyTssTable(Pipe, 0, kTssStage0);
            PipelineApplyTssTable(Pipe, 1, kTssStage1);
            PipelineApplyTssTable(Pipe, 2, kTssStage2);
            PipelineApplyRsTable(Pipe, kRsPanelBoth);
            Pipe->VerifyState();
            return 1;
        }
        if (This->Shape != nullptr)
        {
            Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(This->Shape, 0));
            Pipe->SetTextureStageState(0, 0xb, 0);
            PipelineApplyTssTable(Pipe, 0, kTssStage0);
            PipelineApplyTssTable(Pipe, 1, kTssStage2);
            PipelineApplyRsTable(Pipe, kRsPanelBoth);
            return 1;
        }
        return 0;
    }

    // The list erase FUN_0050b170 (0x0050b1ad..0x0050b1dc) and FUN_0050a170 (FUN_00516f70 per
    // node) run: every node unlinked and freed, +0x6c counting down. No null test.
    void ListUnlinkAndDelete(FTexturePanelsListNode* Sentinel, FTexturePanels* Owner)
    {
        FTexturePanelsListNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            FTexturePanelsListNode* const Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            operator delete(Node);
            Owner->ListCount -= 1;
            Node = Next;
        }
    }

    void PushCloudsNode(FTexturePanels* This, FNebulaClouds* Clouds)
    {
        FTexturePanelsListNode* const Sentinel = This->ListSentinel;
        FTexturePanelsListNode* const Node =
            static_cast<FTexturePanelsListNode*>(operator new(sizeof(FTexturePanelsListNode)));
        Node->Clouds = Clouds;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev->Next = Node;
        Sentinel->Prev = Node;
        This->ListCount += 1;
    }

    // Camera vt+0x0c -> world position, copied as 3 floats. The slot is __stdcall with `this`
    // on the stack (0050b21e PUSH EBP; CALL [EAX+0xc]), as in space_camera.h.
    void CameraReadPos(void* Camera, float* Out3)
    {
        float const* const Pos = ((ICamera*)Camera)->GetPosition();
        Out3[0] = Pos[0];
        Out3[1] = Pos[1];
        Out3[2] = Pos[2];
    }
} // namespace

// -------------------------------------------------------------------- FUN_0050a040

FTexturePanels::FTexturePanels()
{
    // FUN_0050a040 (thiscall). No test of `this` (FUN_0050bfa0 constructs only a non-null
    // block). The memset only covers what retail leaves as it was (the padding and the
    // allocator byte at +0x64, which retail copies from an uninitialised stack byte); every
    // field is stored below as retail stores it.
    memset(ShapePadding, 0, sizeof(ShapePadding));
    memset(&Zone, 0, sizeof(FTexturePanels) - offsetof(FTexturePanels, Zone));
    // List sentinel 0xc (Next/Prev self).
    FTexturePanelsListNode* const Sentinel =
        static_cast<FTexturePanelsListNode*>(operator new(sizeof(FTexturePanelsListNode)));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Sentinel->Clouds = nullptr;
    ListSentinel = Sentinel;
    ListCount = 0;
    ThicknessEdge = 0.1f;
    InnerEdge = 0.1f;
    OuterEdge = 0.1f;
    bLoaded = 0;
    Zone = nullptr;
    Visibility = 0.0f;
    MeshBuffer = nullptr;
    PieceCount = 0;
    Pieces = nullptr;
    Field34 = 0;
    NumSegments = 0x20;
    TransitionDist = 150.0f;
    DetailTileU = 10.0f;
    DetailTileV = 2;
    ColorR = 1.0f;
    ColorG = 1.0f;
    ColorB = 1.0f;
    Opacity = 1.0f;
    Shape = nullptr;
    DetailShape = nullptr;
}

// -------------------------------------------------------------------- FUN_0050a120

IPlanetRing::~IPlanetRing()
{
}

// -------------------------------------------------------------------- FUN_0050b170

void FTexturePanels::Clear()
{
    // 0050b173..0050b178: only a loaded ring is cleared.
    if (bLoaded == 0)
        return;

    // 0050b17f..0050b1ab: every clouds object gets FUN_005501e0 and is freed; the node keeps
    // its (now dangling) pointer until the erase below.
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
    {
        if (N->Clouds != nullptr)
        {
            NebulaCloudsReleaseBits(N->Clouds);
            operator delete(N->Clouds);
        }
    }
    // 0050b1ad..0050b1dc: every node erased, +0x6c counting down.
    ListUnlinkAndDelete(Sentinel, this);

    // 0050b1de..0050b1f0: the ring mesh (+0x38) and the pieces (+0x40) are freed but neither
    // pointer is cleared, and +0x34 / +0x3c keep their values. Clear runs from Load (which
    // reassigns all four unless the zone is missing) and from the destructor.
    operator delete(MeshBuffer);
    operator delete(Pieces);

    Shapes.Clear(); // 0050b1f6: FUN_00425a20 on +0x04

    Zone = nullptr; // 0050b1fd
    bLoaded = 0;    // 0050b204
}

// -------------------------------------------------------------------- FUN_0050a170 / 0050a150

FTexturePanels::~FTexturePanels()
{
    // FUN_0050a170 (thiscall), 0x0050a170..0x0050a231: no test of `this` or of the sentinel.
    // FUN_0050b170; every list node erased (FUN_00516f70); the sentinel freed, +0x68 / +0x6c
    // zeroed; the ShapeDB torn down; the base vtable.
    FTexturePanels::Clear(); // 0x0050a1a1: direct call during destruction
    ListUnlinkAndDelete(ListSentinel, this);
    operator delete(ListSentinel);
    ListSentinel = nullptr;
    ListCount = 0;
}

// -------------------------------------------------------------------- FUN_0050bfa0

FTexturePanels* TexturePanelsCreate()
{
    // FUN_0050bfa0, 0x0050bfa0..0x0050c035: new(0x78), FUN_0050a040 only on a non-null block;
    // the node is appended either way.
    FTexturePanels* Panels = new FTexturePanels;

    FTexturePanelsGlobalNode* const Node =
        static_cast<FTexturePanelsGlobalNode*>(operator new(sizeof(FTexturePanelsGlobalNode)));
    Node->Panels = Panels;
    Node->Next = &G_PanelsListSentinel;
    Node->Prev = G_PanelsListSentinel.Prev;
    G_PanelsListSentinel.Prev->Next = Node;
    G_PanelsListSentinel.Prev = Node;
    G_PanelsListCount += 1;
    return Panels;
}

// StarSys tick FUN_00554eb0, 00554f44..00554f61: FUN_00509f20 hands out this list and every
// entry gets slot 3 (LAB_0050b210) with (camera, dt). 0050b2a5 forwards the dt as
// FUN_00550d40's SECOND argument.
void TexturePanelsUpdateAll(void* Camera, float Dt)
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->Update(Camera, Dt);
}

// FUN_00509f60 — store the panels detail (DAT_00613324) and hand it to slot 12 (+0x30) of
// every live TexturePanels. Caller: the Nebula perf option FUN_00509b80.
void TexturePanelsApplyDetailAll(float Detail)
{
    G_PanelsDetail = Detail;
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->ApplyDetail(G_PanelsDetail);
}

// FUN_00509f30 — slot 5 (+0x14, DrawPanels) with the camera on every live TexturePanels.
// Caller: the space draw pass FUN_0051f950 at 0051fa50, between bucket 0 and FUN_00520170.
void TexturePanelsDrawAll(void* Camera)
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->DrawPanels(Camera);
}

// FUN_00509f20 (&DAT_0067505c) followed by the callers' MOV EAX,[EAX+4].
FTexturePanelsGlobalNode* TexturePanelsListSentinel()
{
    return &G_PanelsListSentinel;
}

// FUN_0050c040 -- slot 13 (+0x34, the deleting destructor) with 1 on every non-null
// TexturePanels (0050c051..0050c069), then every node unlinked and freed with the count
// dropping (0050c06b..0050c09c).
void TexturePanelsDestroyAll()
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
    {
        if (N->Panels != nullptr)
            delete N->Panels;
    }
    FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next;
    while (N != &G_PanelsListSentinel)
    {
        FTexturePanelsGlobalNode* const Next = N->Next;
        N->Prev->Next = N->Next;
        N->Next->Prev = N->Prev;
        operator delete(N);
        G_PanelsListCount -= 1;
        N = Next;
    }
}

// -------------------------------------------------------------------- slot10 FUN_00504870

Universe::IZone const* FTexturePanels::GetZone()
{
    return Zone; // MOV EAX,[ECX+0x1c]; RET
}

// -------------------------------------------------------------------- slot9 LAB_0050bf30

float FTexturePanels::GetVisibility()
{
    return Visibility; // FLD [ECX+0x70]; RET -- no test
}

// -------------------------------------------------------------------- slot6 LAB_0050bcd0

int FTexturePanels::GetPieceCount()
{
    return PieceCount; // MOV EAX,[ECX+0x3c]; RET
}

namespace
{
    char const kPlanetRingCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\PlanetRing.cpp"; // 005db2e4
    char const kPieceOutOfRange[] = "piece num specified in plantary ring is out of range";   // 005db318
    char const kShapeNotFoundForRings[] =
        "%s(%d) : *** ERROR: shape \"%s\" not found for planet rings"; // 005db2a8
}

// -------------------------------------------------------------------- slot7 LAB_0050bce0

float const* FTexturePanels::GetPiece(int Index)
{
    // Signed JGE against +0x3c; in range -> &Pieces[Index] + 8 (the piece centre), else
    // FDUMP (line 0x279) and null.
    if (Index < PieceCount)
        return Pieces[Index].Center;
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x279, kPieceOutOfRange);
    return nullptr;
}

// -------------------------------------------------------------------- slot8 LAB_0050bd20

float FTexturePanels::GetPieceFloat(int Index)
{
    // Same bounds test; in range -> float at piece +0x14, else FDUMP (line 0x284) and
    // DAT_005c7474 (0.0f).
    if (Index < PieceCount)
        return Pieces[Index].Radius;
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x284, kPieceOutOfRange);
    return kZero;
}

// -------------------------------------------------------------------- slot11 FUN_0050bf40

double FTexturePanels::GetZonePos(float* Out3)
{
    // FUN_0050bf40 (thiscall, RET 4), 0x0050bf40..0x0050bf68: the zone's (+0x1c) position
    // (vt+0x10) copied to Out3, then its radius (vt+0x30) left on the FPU as the result.
    // Nothing is tested.
    Vector const& Pos = Zone->GetPos();
    Out3[0] = Pos.x;
    Out3[1] = Pos.y;
    Out3[2] = Pos.z;
    return static_cast<double>(Zone->GetRadius());
}

// -------------------------------------------------------------------- slot12 LAB_0050bf70

void FTexturePanels::ApplyDetail(float Detail)
{
    // LAB_0050bf70 (thiscall, RET 4), 0x0050bf70..0x0050bf92: FUN_005524a0(detail) on every
    // clouds object of the list at +0x68 (sentinel's next until the sentinel). Nothing is
    // tested.
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        NebulaCloudsApplyDetail(N->Clouds, Detail);
}

namespace
{
    I3DMathEngine* AcquirePanelsMath()
    {
        // DAT_00675074 — same lazy 3DMathEngine as Nebula (FUN_005b73e0).
        static I3DMathEngine* G_PanelsMath = nullptr;
        if (G_PanelsMath == nullptr)
            G_PanelsMath = Acquire3DMathEngine();
        return G_PanelsMath;
    }

    // The zone's vt+0x0c transform (orientation, then position), as retail reads it.
    void ZoneToTransform(Universe::IZone const& Zone, FTransform& Out)
    {
        static_assert(sizeof(FTransform) == sizeof(Transform), "FTransform matches Transform");
        memcpy(&Out, &Zone.GetTransform(), sizeof(Out));
    }
} // namespace

// -------------------------------------------------------------------- slot0 LAB_0050bd70

double FTexturePanels::GetImmersion(Vector const& Point) const
{
    // LAB_0050bd70 (thiscall, RET 4: camera position), listing-traced 0x0050bd70..0x0050bf20.
    // The result is left on the FPU. Nothing is tested.
    // 0x0050bd77..0x0050bdb7: the zone's size (vt+0x28, a Vector by value) fetched three times
    // for x, y and z.
    float const OuterRadius = Zone->GetSize().x;
    float const InnerRadius = Zone->GetSize().y;
    float const HalfHeight = Zone->GetSize().z;
    // 0x0050bdac..0x0050bdf0: outer^2, inner^2, (outer + td)^2 and (inner - td)^2 as floats
    // (td = +0x30).
    float const OuterSq = static_cast<float>(static_cast<double>(OuterRadius) * OuterRadius);
    float const InnerSq = static_cast<float>(static_cast<double>(InnerRadius) * InnerRadius);
    double const OuterPlus = static_cast<double>(OuterRadius) + TransitionDist;
    float const SoftOuterSq = static_cast<float>(OuterPlus * OuterPlus);
    double const InnerMinus = static_cast<double>(InnerRadius) - TransitionDist;
    float const SoftInnerSq = static_cast<float>(InnerMinus * InnerMinus);

    // 0x0050bdf2..0x0050be27: the zone transform (vt+0x0c) copied; the camera position into it
    // through DAT_00675074 (FUN_005b73e0 when null) InverseTransform (+0x3c).
    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);
    FVector Local;
    AcquirePanelsMath()->InverseTransform(Local, ZoneXf,
                                          *reinterpret_cast<FVector const*>(&Point));

    // 0x0050be2a..0x0050be69: the height factor over |local y|: below the half height (TEST
    // AH,5 / JP falls through on ordered less) 1.0; otherwise 1 - (|y| - h) / td (double
    // DAT_005c89b8) through a float, and not above 0 (TEST AH,0x41 / JNZ, unordered too)
    // returns 0.0 * factor.
    double const AbsY = std::fabs(static_cast<double>(Local.Y));
    float Factor = 1.0f;
    if (!(AbsY < static_cast<double>(HalfHeight)))
    {
        // FST keeps the unrounded value on the FPU; FCOMP tests that one.
        double const FactorExt =
            1.0 - (AbsY - HalfHeight) / static_cast<double>(TransitionDist);
        Factor = static_cast<float>(FactorExt);
        if (!(FactorExt > 0.0))
            return 0.0 * static_cast<double>(Factor);
    }

    // 0x0050be71..0x0050be90: r^2 = x*x + z*z on the FPU, stored as a float; above the soft
    // outer square (TEST AH,0x41 / JZ: ordered greater) -> 0.0 * factor.
    double const RadialSq = static_cast<double>(Local.X) * Local.X +
                            static_cast<double>(Local.Z) * Local.Z;
    double const RadialSqF = static_cast<double>(static_cast<float>(RadialSq));
    if (RadialSq > static_cast<double>(SoftOuterSq))
        return 0.0 * static_cast<double>(Factor);

    // 0x0050be92..0x0050bebd: beyond the outer square -> the outer ramp.
    if (RadialSqF > static_cast<double>(OuterSq))
        return (static_cast<double>(SoftOuterSq) - RadialSqF) /
               (static_cast<double>(SoftOuterSq) - OuterSq) * Factor;

    // 0x0050bec0..0x0050bedf: beyond the inner square -> 1.0 * factor.
    if (RadialSqF > static_cast<double>(InnerSq))
        return 1.0 * static_cast<double>(Factor);

    // 0x0050bee2..0x0050bf0d: beyond the soft inner square -> the inner ramp; otherwise
    // (LAB_0050bf10) 0.0 * factor.
    if (RadialSqF > static_cast<double>(SoftInnerSq))
        return (RadialSqF - SoftInnerSq) /
               (static_cast<double>(InnerSq) - SoftInnerSq) * Factor;
    return 0.0 * static_cast<double>(Factor);
}

// -------------------------------------------------------------------- slot3 LAB_0050b210

void FTexturePanels::Update(void* Camera, float Dt)
{
    // 0050b215..0050b237: camera vt+0x0c (__stdcall), copied to a local.
    float CamPos[3];
    CameraReadPos(Camera, CamPos);

    // Retail LAB_0050b210 (.text:0050b23d-0050b288): the camera position into the zone's
    // vt+0x0c frame (3DMathEngine InverseTransform, slot +0x3c), then, when vt+0x34(pos, 0)
    // says the camera is outside, vt+0x44(pos) -- both into the same local, which nothing
    // reads afterwards. Slot 0 below gets the camera's WORLD position (.text:0050b28d).
    {
        Vector CamV;
        CamV.x = CamPos[0];
        CamV.y = CamPos[1];
        CamV.z = CamPos[2];
        FTransform ZoneXf;
        ZoneToTransform(*Zone, ZoneXf);
        FVector Local;
        AcquirePanelsMath()->InverseTransform(Local, ZoneXf, *reinterpret_cast<FVector const*>(&CamV));
        if (!Zone->InZone(CamV, 0.0f))
        {
            Vector const Surface = Zone->GetSurfacePoint(CamV);
            (void)Surface;
        }
    }

    Visibility = static_cast<float>(GetImmersion(*reinterpret_cast<Vector const*>(CamPos)));

    // 0050b299..0050b2ba: every Clouds on the list gets FUN_00550d40(camera, dt, +0x70, null).
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        NebulaCloudsUpdate(N->Clouds, Camera, Dt, Visibility, nullptr);
}

// -------------------------------------------------------------------- slot4 LAB_0050b510

void FTexturePanels::DrawPiece(void* Camera, int PieceIndex)
{
    // __thiscall, RET 8: [ESP+0xd4] = camera, [ESP+0xd8] = piece (frame 0xd0 at 0050b53a).
    // 0050b52f..0050b54c.
    if (bLoaded == 0)
        return;
    if (PieceIndex < 0)
        return;
    if (PieceIndex >= PieceCount)
        return;

    // 0050b552..0050b606: three FUN_00405840 savers, each read then forced through its own
    // Pipeline/State fields.
    FRenderStateRestorer Save16(0x16);
    Save16.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save16.State, 1);
    FRenderStateRestorer Save0e(0xe);
    Save0e.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save0e.State, 0);
    FRenderStateRestorer Save17(0x17);
    Save17.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save17.State, 4);

    // 0050b60c..0050b621: FUN_004223d0 (__fastcall pass-through, ECX = a stack address whose
    // value nobody reads) then FUN_00422410.
    Win3DSetTextureFilterLinear(nullptr);
    Win3DSetTextureFilterPoint();

    // 0050b626..0050b687: the zone's vt+0x0c transform (12 dwords copied), the camera's
    // position (vt+0x0c, __stdcall: PUSH ESI), and 3DMathEngine (DAT_00675074, lazy
    // FUN_005b73e0) vt+0x3c InverseTransform into [ESP+0x20] -- a scratch the loop overwrites
    // with vertex b before anything reads it.
    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);
    FVector const CamPos = *(FVector const*)((ICamera*)Camera)->GetPosition();
    FVector Scratch;
    AcquirePanelsMath()->InverseTransform(Scratch, ZoneXf, CamPos);

    // 0050b68a..0050b6bf: FUN_004215b0 identity rotation, zero translation (stores 0x98/0x94/
    // 0x90 = [11]/[10]/[9]), FUN_00422690(camera, &matrix).
    float World12[12];
    Win3DIdentityRotation3x3(World12);
    World12[11] = 0.0f;
    World12[10] = 0.0f;
    World12[9] = 0.0f;
    Win3DBindModelview(Camera, World12);

    // 0050b6c2..0050b6cb: FUN_0050b2d0; zero skips straight to the restorers (0050bac4).
    if (SetupPanelPipeline(this) != 0)
    {
        // 0050b6d1..0050b6ef: piece record stride 0x18, +0 first segment, +4 last.
        FTexturePanelPiece const* const Piece = &Pieces[PieceIndex];
        int const First = Piece->FirstSegment;
        int const Last = Piece->LastSegment;

        // 0050b6e1..0050b712: the batcher's pipeline (+0x18, DAT_00675030) -- the old one is
        // released, the current DALib::Pipeline is stored and then AddRef'd.
        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (G_PanelBatcher.Pipeline != nullptr)
        {
            G_PanelBatcher.Pipeline->Release();
            G_PanelBatcher.Pipeline = nullptr;
        }
        if (Pipe != nullptr)
        {
            G_PanelBatcher.Pipeline = Pipe;
            Pipe->AddRef();
        }

        // 0050b715..0050b730: FUN_0050c190, primitive 5 (triangle strip), +0x28/+0x29 = 0.
        PanelBatcherReset(&G_PanelBatcher);
        G_PanelBatcher.Field00 = 5;
        G_PanelBatcher.Flag28 = 0;
        G_PanelBatcher.Flag29 = 0;
        // 0050b737..0050b780: FLD float, FMUL double .rdata 0x005c89c0 (255.0), ftol, store AL --
        // the product is taken in extended precision (exact for float * 255.0, so a double
        // product matches), truncated toward zero, and only the low byte is kept. No clamp.
        double const K255 = 255.0;
        G_PanelBatcher.ColorR = static_cast<unsigned char>(static_cast<int>(ColorR * K255));
        G_PanelBatcher.ColorG = static_cast<unsigned char>(static_cast<int>(ColorG * K255));
        G_PanelBatcher.ColorB = static_cast<unsigned char>(static_cast<int>(ColorB * K255));
        G_PanelBatcher.ColorA = static_cast<unsigned char>(static_cast<int>(Opacity * K255));

        // 0050b77e..0050ba16: segment I in [First, Last]; ESI = 2*I indexes the ring mesh
        // (+0x38, 0xc per vertex, +0x34 vertices), wrapped by ONE subtraction each.
        int Twice = First * 2;
        for (int I = First; I <= Last; ++I)
        {
            int const MeshCount = Field34;
            int IndexA = Twice;
            if (IndexA >= MeshCount)
                IndexA -= MeshCount;
            int IndexB = Twice + 1;
            if (IndexB >= MeshCount)
                IndexB -= MeshCount;
            float const* const Mesh = static_cast<float const*>(MeshBuffer);
            float const* const A = Mesh + IndexA * 3;
            float const* const B = Mesh + IndexB * 3;
            float const Ax = A[0];
            float const Ay = A[1];
            float const Az = A[2];
            float const Bx = B[0];
            float const By = B[1];
            float const Bz = B[2];

            // 0050b7d2..0050b814: odd segments take the shape's right edge (+0x10) and the
            // integer detail_tile V (+0x60), even ones the left edge (+0x08) and 0; the int
            // is FILD'ed and kept as a float (FST [ESP+0x14]).
            FTexShape const* const Shape = this->Shape;
            float const U = ((I & 1) != 0) ? Shape->U1 : Shape->U0;
            int const DetailUInt = ((I & 1) != 0) ? DetailTileV : 0;
            float const DetailU = static_cast<float>(DetailUInt);

            // 0050b818..0050b835: vertex a -- V = shape top (+0x0c), detail V = 0.
            G_PanelBatcher.StageV = Shape->V0;
            G_PanelBatcher.StageU = U;
            G_PanelBatcher.StageV1 = 0.0f;
            G_PanelBatcher.StageU1 = DetailU;
            // 0050b83b..0050b9c2: an inline copy of FUN_0050bb60 (same Flag29 quad
            // duplication, colour/UV/XYZ stores, count++ and FUN_0050c120 grow).
            PanelBatcherPushVert(&G_PanelBatcher, Ax, Ay, Az);

            // 0050b9c7..0050ba03: vertex b -- V = shape bottom (+0x14), detail V =
            // detail_tile U (+0x5c); FUN_0050bb60(b.x, b.y, b.z).
            G_PanelBatcher.StageV = Shape->V1;
            G_PanelBatcher.StageU = U;
            G_PanelBatcher.StageV1 = DetailTileU;
            G_PanelBatcher.StageU1 = DetailU;
            PanelBatcherPushVert(&G_PanelBatcher, Bx, By, Bz);

            Twice += 2;
        }

        // 0050ba1c..0050ba87: flush. A non-empty batch closes the loop when +0x28 is set
        // (vertex 0 copied past the end, count + 1, no grow), then draws through the held
        // pipeline's vt+0x114 (prim +0x00, FVF +0x0c, verts, count, 0). The held pipeline is
        // released either way.
        int Count = G_PanelBatcher.VertCount;
        if (Count != 0)
        {
            if (G_PanelBatcher.Flag28 != 0)
            {
                memcpy(&G_PanelBatcher.Verts[Count], &G_PanelBatcher.Verts[0], sizeof(FPanelVert20));
                Count += 1;
                G_PanelBatcher.VertCount = Count;
            }
            if (G_PanelBatcher.Pipeline != nullptr)
            {
                G_PanelBatcher.Pipeline->DrawPrimitive(G_PanelBatcher.Field00,
                                                       static_cast<unsigned int>(G_PanelBatcher.Fvf),
                                                       G_PanelBatcher.Verts,
                                                       static_cast<unsigned int>(Count), nullptr);
            }
        }
        if (G_PanelBatcher.Pipeline != nullptr)
        {
            G_PanelBatcher.Pipeline->Release();
            G_PanelBatcher.Pipeline = nullptr;
        }

        // 0050ba91..0050baac: with a detail shape, stage 1 goes back to DAT_005db178.
        if (DetailShape != nullptr)
            PipelineApplyTssPairTable(DALib::Pipeline, 1, kTssStage2);

        // 0050baaf..0050babe: D3DRS 0x1b (ALPHABLENDENABLE) = 0.
        DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    }

    // 0050bac4..0050baf5: the savers unwind 0x17, 0xe, 0x16.
}

// -------------------------------------------------------------------- slot5 LAB_0050bc90

// .text:0050bc90..0050bccb (__thiscall, RET 4). No null tests on This, the list or a node's clouds.
void FTexturePanels::DrawPanels(void* Camera)
{
    // 0x0050bc90..0x0050bca7: byte +0x74, then +0x70 strictly greater than 0 (TEST AH,0x41 / JNE).
    if (bLoaded == 0)
        return;
    if (!(Visibility > kZero))
        return;
    // 0x0050bca9..0x0050bcc6: every node of the +0x68 list -> FUN_00551500(node+8, camera, 0).
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        TexturePanelDrawClouds(N->Clouds, Camera, nullptr);
}

// -------------------------------------------------------------------- slot1 LAB_0050a240

void FTexturePanels::Load(char const* IniPath,
                                       char const* Nickname)
{
    // 0050a263..0050a271: a loaded ring is cleared first (slot 2).
    if (bLoaded != 0)
        Clear();

    // 0050a274..0050a292: Universe::get_zone(FUN_00554e90(), Nickname), stored at +0x1c, then
    // the zone's vt+0x18 (GetShape) with the result dropped. Retail dereferences the zone here
    // with no null test.
    Zone = Universe::get_zone(QueryActiveSystemIdStarSys(), Nickname);
    Zone->GetShape();

    // 0050a295..0050a2be: the defaults every load starts from (colour and detail_tile keep
    // whatever they held).
    OuterEdge = 0.1f;
    InnerEdge = 0.1f;
    ThicknessEdge = 0.1f;
    Opacity = 1.0f;
    Shape = nullptr;
    DetailShape = nullptr;
    NumSegments = 0x20;
    TransitionDist = 150.0f;

    // 0050a2c5..0050a303: the path is resolved against DALib::Data into a MAX_PATH buffer. A
    // failed open or an empty file skips the parse but NOT the rest (both jump to 0050a808).
    INI_Reader Reader;
    char FullPath[0x104];
    GetFileSysPath(FullPath, IniPath, DALib::Data);
    if (Reader.open(FullPath, false) && Reader.read_header())
    {
        do
        {
            if (Reader.is_header("TexturePanels"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                    {
                        // 0050a376: FUN_00425b30 on +0x04 with no base filesystem.
                        char const* Full = GetFullFilename(Reader.get_value_string());
                        Shapes.Load(Full, nullptr);
                    }
                }
            }
            else if (Reader.is_header("Exterior"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("color"))
                    {
                        ColorR = Reader.get_value_float(0) * kColorScale;
                        ColorG = Reader.get_value_float(1) * kColorScale;
                        ColorB = Reader.get_value_float(2) * kColorScale;
                    }
                    else if (Reader.is_value("shape"))
                    {
                        // 0050a475..0050a53c: the inlined ShapeDB.h find (its own warning on a
                        // miss), then PlanetRing.cpp line 0x108 when the result is null.
                        Shape = Shapes.Find(Reader.get_value_string());
                        if (Shape == nullptr)
                            FDUMP(0x100001, kShapeNotFoundForRings, kPlanetRingCppPath, 0x108,
                                  Reader.get_value_string());
                    }
                    else if (Reader.is_value("detail_shape"))
                    {
                        // 0050a557..0050a60a: same, line 0x10f; +0x58 is zeroed before the find.
                        DetailShape = nullptr;
                        DetailShape =
                            Shapes.Find(Reader.get_value_string());
                        if (DetailShape == nullptr)
                            FDUMP(0x100001, kShapeNotFoundForRings, kPlanetRingCppPath, 0x10f,
                                  Reader.get_value_string());
                    }
                    else if (Reader.is_value("detail_tile"))
                    {
                        DetailTileU = Reader.get_value_float(0);
                        DetailTileV = static_cast<int>(Reader.get_value_float(1)); // ftol
                    }
                    else if (Reader.is_value("opacity"))
                        Opacity = Reader.get_value_float(0);
                    else if (Reader.is_value("thickness_edge"))
                        ThicknessEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("inner_edge"))
                        InnerEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("outer_edge"))
                        OuterEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("num_segments"))
                        NumSegments = static_cast<int>(Reader.get_value_float(0)); // ftol
                    else if (Reader.is_value("transition_dist"))
                        TransitionDist = Reader.get_value_float(0);
                }
            }
            else if (Reader.is_header("Clouds"))
            {
                // 0050a735..0050a7e8: new FNebulaClouds (0x12c, FUN_00550180), parsed from this
                // section against the ring's own ShapeDB (FUN_00550240), owner at +0xc4, the three
                // floats below, then appended to the +0x68 list (FUN_005999b0) and +0x6c bumped.
                FNebulaClouds* const Clouds =
                    static_cast<FNebulaClouds*>(operator new(sizeof(FNebulaClouds)));
                NebulaCloudsConstruct(Clouds);
                NebulaCloudsParseIni(Clouds, Reader, &Shapes);
                *reinterpret_cast<FTexturePanels**>(Clouds->Raw + 0xc4) = this;
                *reinterpret_cast<float*>(Clouds->Raw + 0x104) = 0.5f; // 0x3f000000
                *reinterpret_cast<float*>(Clouds->Raw + 0x108) = 0.9f; // 0x3f666666
                *reinterpret_cast<float*>(Clouds->Raw + 0x10c) = 1.1f; // 0x3f8ccccd
                PushCloudsNode(this, Clouds);
            }
        } while (Reader.read_header());
    }

    // 0050a808..0050a89d: the ring counts as loaded from here on, zone or not; without a zone
    // it FDUMPs and returns.
    bLoaded = 1;
    if (Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x135,
              "planet ring must have zone");
        return;
    }

    // 0050a8a2..0050a8d0: the zone's vt+0x28 (GetSize) three times -- x and y kept, the third
    // result dropped.
    float const SizeX = Zone->GetSize().x;
    float const SizeY = Zone->GetSize().y;
    Zone->GetSize();

    // 0050a8d3..0050a91d: step = float(2pi / NumSegments) (DAT_005c97f8 is the double of the
    // float 2pi), 2 * NumSegments mesh vertices (+0x34) of 0xc bytes (+0x38, not cleared), eight
    // pieces (+0x3c) of 0x18 bytes (+0x40, not cleared).
    double const kTwoPi = 6.2831854820251465; // DAT_005c97f8
    float const AngleStep = static_cast<float>(kTwoPi / static_cast<double>(NumSegments));
    Field34 = NumSegments * 2;
    MeshBuffer = operator new(static_cast<size_t>(NumSegments * 2 * 3) * 4u);
    PieceCount = 8;
    Pieces = static_cast<FTexturePanelPiece*>(operator new(0xc0));

    // 0050a920..0050a934: the zone's vt+0x0c transform, copied (12 dwords).
    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);

    // 0050a936..0050aa47: segment I puts {sin*SizeX, 0, cos*SizeX} at mesh vertex 2I and
    // {sin*SizeY, 0, cos*SizeY} at 2I+1 (the ring's two edges), each run through
    // 3DMathEngine vt+0x28 (TransformXform) with the zone transform. Both indices are wrapped
    // by one subtraction of +0x34 (never taken for I < NumSegments). The products are taken in
    // extended precision off FSIN / FCOS and stored as floats; the angle accumulates in a float.
    float* const Mesh = static_cast<float*>(MeshBuffer);
    float Angle = 0.0f;
    int Twice = 0;
    for (int Segment = 0; Segment < NumSegments; ++Segment)
    {
        double const Sin = sin(static_cast<double>(Angle));
        double const Cos = cos(static_cast<double>(Angle));
        int OuterIndex = Twice;
        if (OuterIndex >= Field34)
            OuterIndex -= Field34;
        int InnerIndex = Twice + 1;
        if (InnerIndex >= Field34)
            InnerIndex -= Field34;

        // [S+0x70] first (sin*y, 0, y*cos), then [S+0x58] (sin*x, 0, cos*x).
        FVector LocalY;
        LocalY.X = static_cast<float>(Sin * SizeY);
        LocalY.Y = 0.0f;
        LocalY.Z = static_cast<float>(SizeY * Cos);
        FVector LocalX;
        LocalX.X = static_cast<float>(Sin * SizeX);
        LocalX.Y = 0.0f;
        LocalX.Z = static_cast<float>(Cos * SizeX);

        FVector World;
        AcquirePanelsMath()->TransformXform(World, ZoneXf, LocalX);
        Mesh[OuterIndex * 3 + 0] = World.X;
        Mesh[OuterIndex * 3 + 1] = World.Y;
        Mesh[OuterIndex * 3 + 2] = World.Z;
        AcquirePanelsMath()->TransformXform(World, ZoneXf, LocalY);
        Mesh[InnerIndex * 3 + 0] = World.X;
        Mesh[InnerIndex * 3 + 1] = World.Y;
        Mesh[InnerIndex * 3 + 2] = World.Z;

        Angle = static_cast<float>(static_cast<double>(Angle) + AngleStep);
        Twice += 2;
    }

    // 0050aa4f..0050ac5d: the pieces. Span = float(NumSegments / PieceCount) (FILD / FIDIV);
    // the running end stays on the FPU (FADD float each piece). Piece P runs from the previous
    // piece's last segment (0 for the first) to ftol(1e-4f + End), and gets the bounding box
    // of mesh vertices 2*First .. 2*Last+1 (one wrap by +0x34): centre (max + min) * 0.5,
    // radius the length of max - centre.
    float const Span = static_cast<float>(static_cast<double>(NumSegments) /
                                          static_cast<double>(PieceCount));
    float const kLastBias = 9.99999975e-05f; // DAT_005db098 (float 1e-4)
    double End = 0.0;                         // DAT_005c7474
    int PrevLast = 0;
    // The box lives across pieces: a piece with First > Last reuses the previous one.
    float MinX = 0.0f;
    float MinY = 0.0f;
    float MinZ = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;
    float MaxZ = 0.0f;
    for (int PieceIndex = 0; PieceIndex < PieceCount; ++PieceIndex)
    {
        FTexturePanelPiece* const Piece = &Pieces[PieceIndex];
        End += Span;
        Piece->FirstSegment = PrevLast;
        Piece->LastSegment = static_cast<int>(static_cast<double>(kLastBias) + End); // ftol
        PrevLast = Piece->LastSegment;

        int const FirstVertex = Piece->FirstSegment * 2;
        int const LastVertex = Piece->LastSegment * 2 + 1;
        for (int Vertex = FirstVertex; Vertex <= LastVertex; ++Vertex)
        {
            int MeshIndex = Vertex;
            if (Vertex >= Field34)
                MeshIndex = Vertex - Field34;
            float const X = Mesh[MeshIndex * 3 + 0];
            float const Y = Mesh[MeshIndex * 3 + 1];
            float const Z = Mesh[MeshIndex * 3 + 2];
            if (Vertex == FirstVertex)
            {
                MaxX = X;
                MinX = X;
                MaxY = Y;
                MinY = Y;
                MaxZ = Z;
                MinZ = Z;
            }
            // 0050ab23..0050aba3, per axis: TEST AH,5 / JP falls through only for "less" (new
            // minimum); otherwise TEST AH,0x41 / JNZ skips unless "greater" (new maximum).
            if (X < MinX)
                MinX = X;
            else if (X > MaxX)
                MaxX = X;
            if (Y < MinY)
                MinY = Y;
            else if (Y > MaxY)
                MaxY = Y;
            if (Z < MinZ)
                MinZ = Z;
            else if (Z > MaxZ)
                MaxZ = Z;
        }

        // 0050abc6..0050ac56: DAT_005c75e8 is the double 0.5.
        Piece->Center[0] = static_cast<float>((static_cast<double>(MaxX) + MinX) * 0.5);
        Piece->Center[1] = static_cast<float>((static_cast<double>(MaxY) + MinY) * 0.5);
        Piece->Center[2] = static_cast<float>((static_cast<double>(MaxZ) + MinZ) * 0.5);
        double const Dx = static_cast<double>(MaxX) - Piece->Center[0];
        double const Dy = static_cast<double>(MaxY) - Piece->Center[1];
        double const Dz = static_cast<double>(MaxZ) - Piece->Center[2];
        Piece->Radius = static_cast<float>(sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
    }

    // 0050ac65..0050ac88: the perf option with IDS 0x658 (high_detail_textures, FUN_005b3770)
    // drops the detail shape when its applied value (+0x14) is below 0.4f (DAT_005d9638;
    // FCOMP + TEST AH,1 + JZ skips unless C0, i.e. less or unordered).
    FPerfOption const* const HighDetail = FindPerfOptionByIds(0x658);
    if (HighDetail != nullptr && !(HighDetail->Applied >= 0.4f))
        DetailShape = nullptr;

    // 0050ac8b..0050ace2: a shape whose frame-0 handle (FUN_00425640) the pipeline rejects
    // (IsTexture, slot 47, non-zero) is dropped.
    if (Shape != nullptr &&
        DALib::Pipeline->IsTexture(TexShapeGetGpuHandle(Shape, 0)) != 0)
        Shape = nullptr;
    if (DetailShape != nullptr &&
        DALib::Pipeline->IsTexture(TexShapeGetGpuHandle(DetailShape, 0)) != 0)
        DetailShape = nullptr;

    // 0050ace5..0050ad3f: with any texture, FUN_0050b2d0 then VerifyState (slot 68); a
    // failure drops the detail shape and tries again, a second failure drops the shape. Without
    // a detail shape (from the start or dropped) the opacity is halved (DAT_005c75e8).
    if (Shape != nullptr || DetailShape != nullptr)
    {
        SetupPanelPipeline(this);
        if (DALib::Pipeline->VerifyState() < 0 && DetailShape != nullptr)
        {
            DetailShape = nullptr;
            SetupPanelPipeline(this);
            if (DALib::Pipeline->VerifyState() < 0)
                Shape = nullptr;
        }
        if (DetailShape == nullptr)
            Opacity = static_cast<float>(static_cast<double>(Opacity) * 0.5);
    }

    // 0050ad42..0050b150: the INI_Reader's destructor, inlined.
}

// -------------------------------------------------------------------- FUN_00551500

namespace
{
    // DAT_00679864 -- incremented once per written puff (0x005522af..0x005522cf). Those two
    // instructions are its only references (XREF[2]), so nothing in retail reads it back.
    int G_CloudsPuffsDrawn = 0;

    // DAT_00679860 through the lazy test retail repeats inline at each of its three reads
    // (0x0055156e, 0x005519f3, 0x00551b94): FUN_005b73e0 on first use.
    I3DMathEngine* CloudsMath()
    {
        if (G_CloudsMath == nullptr)
            G_CloudsMath = Acquire3DMathEngine();
        return G_CloudsMath;
    }

    // FUN_00507e70 (cdecl, one float): the estimate Y = 0x5f3759df - (bits >> 1) and one Newton
    // step (1.5f - Y * Y * X * 0.5f) * Y, returned on the FPU (same body as cli_nebula.cpp's
    // NebulaInvSqrt, which lives in that file's anonymous namespace).
    double CloudsInvSqrt(float X)
    {
        int XBits = 0;
        memcpy(&XBits, &X, sizeof(XBits));
        int const YBits = 0x5f3759df - (XBits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &YBits, sizeof(Y));
        return (1.5f - static_cast<double>(Y) * Y * X * 0.5f) * Y;
    }

    // MSVCRT _ftol (FUN_005b7ec0): truncation toward zero into EDX:EAX; the callers here keep EAX.
    unsigned CloudsFtol(double X)
    {
        return static_cast<unsigned>(static_cast<long long>(X));
    }
}

// .text:00551500..0055240d (__thiscall, RET 8): this = the clouds object, arg1 = the camera,
// arg2 = the exclusion manager handed to FUN_005513b0. No null tests anywhere: not on the camera,
// its orientation, the pipeline, the vertex-buffer manager, the puff table or a puff.
void TexturePanelDrawClouds(FNebulaClouds* Clouds, void* Camera, void* ExclusionCtx)
{
    unsigned char* const B = Clouds->Raw;
    ICamera* const Cam = static_cast<ICamera*>(Camera);

    // 0x00551524..0x0055153c: camera byte +0xbc -> FUN_005513b0(arg2).
    if (static_cast<unsigned char*>(Camera)[0xbc] != 0)
        NebulaCloudsRegenExcluded(Clouds, ExclusionCtx);

    // 0x00551541..0x0055155f: +0xec, the active count +0x14 and the first shape +0x28.
    if (B[0xec] == 0)
        return;
    if (*reinterpret_cast<int*>(B + 0x14) == 0)
        return;
    if (*reinterpret_cast<FTexShape**>(B + 0x28) == nullptr)
        return;

    // 0x00551565..0x00551593: camera slot 4 (+0x10) -> twelve dwords copied into a local
    // transform (rotation, then position); a local (0, 0, 1).
    FTransform CamXform;
    memcpy(&CamXform, Cam->GetOrientation(), sizeof(CamXform));
    FVector UnitZ = {0.0f, 0.0f, 1.0f};

    // 0x005515a7..0x005515bd: W = I3DMathEngine::Rotate (+0x30)(CamXform, (0, 0, 1)).
    FVector W;
    CloudsMath()->Rotate(W, CamXform, UnitZ);

    // 0x005515c0..0x00551608: C1 = N x W (N = +0x6c), each component FSTP'd as a float, through
    // FUN_00422b80.
    float* const N = reinterpret_cast<float*>(B + 0x6c);
    float C1[3];
    C1[0] = static_cast<float>(static_cast<double>(W.Z) * N[1] - static_cast<double>(W.Y) * N[2]);
    C1[1] = static_cast<float>(static_cast<double>(W.X) * N[2] - static_cast<double>(W.Z) * N[0]);
    C1[2] = static_cast<float>(static_cast<double>(W.Y) * N[0] - static_cast<double>(W.X) * N[1]);

    // 0x0055160d..0x00551680: C2 = W x C1 (FUN_00422b80 into the (0, 0, 1) local), copied to N.
    float C2[3];
    C2[0] = static_cast<float>(static_cast<double>(W.Y) * C1[2] - static_cast<double>(W.Z) * C1[1]);
    C2[1] = static_cast<float>(static_cast<double>(W.Z) * C1[0] - static_cast<double>(W.X) * C1[2]);
    C2[2] = static_cast<float>(static_cast<double>(W.X) * C1[1] - static_cast<double>(W.Y) * C1[0]);
    N[0] = C2[0];
    N[1] = C2[1];
    N[2] = C2[2];

    // 0x00551682..0x005516ce: N scaled by FUN_00507e70 of the float (x*x + y*y) + z*z.
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(N[0]) * N[0] + static_cast<double>(N[1]) * N[1]) +
            static_cast<double>(N[2]) * N[2]);
        double const Inv = CloudsInvSqrt(LenSq);
        N[0] = static_cast<float>(Inv * N[0]);
        N[1] = static_cast<float>(Inv * N[1]);
        N[2] = static_cast<float>(Inv * N[2]);
    }

    // 0x005516d1..0x00551713: R = +0x100; the "ahead" threshold float(+0xb8 * 0.8f)
    // (DAT_005d8d40); the vertex total +0xc * 6; the inner radius float(R * 0.7f) (DAT_005d8e80);
    // the written-vertex counter.
    float const Radius = *reinterpret_cast<float*>(B + 0x100);
    float const AheadThreshold =
        static_cast<float>(static_cast<double>(*reinterpret_cast<float*>(B + 0xb8)) * 0.8f);
    int const VertTotal = *reinterpret_cast<int*>(B + 0xc) * 6;
    float const InnerRadius = static_cast<float>(static_cast<double>(Radius) * 0.7f);
    int VertCount = 0;

    // 0x005516bc / 0x0055171a / 0x0055172e: three {pointer, stride} cursors, EH states 0, 1, 2
    // (unwound through FUN_00552b20, which zeroes both fields): position {0, 0xc}, diffuse
    // {0, 4}, texcoord {0, 8}.
    FBillboardVertexCursor PosCursor(0xc);
    FBillboardVertexCursor DiffCursor(4);
    FBillboardVertexCursor UvCursor(8);
    unsigned char bLocked = 0; // [ESP+0x17]

    // 0x0055173a..0x005517a9: the first shape's alpha probe, cached in its +0x04 while negative.
    // FUN_005b7010(0) seeds the id, TextureLib slot 23 (+0x5c, GetGroupPixelFormat) on the
    // shape's record fills it (result untested), then FUN_00552420, zero-extended into +0x04.
    FTexShape* const Shape0 = *reinterpret_cast<FTexShape**>(B + 0x28);
    if (Shape0->Field04 < 0)
    {
        int Fmt = PixelFormatFromD3D(0);
        DALib::TextureLib->GetGroupPixelFormat(Shape0->Texture, nullptr, &Fmt);
        Shape0->Field04 = static_cast<int>(TextureFormatHasAlpha(Fmt));
    }
    unsigned char const bAlpha = static_cast<unsigned char>(Shape0->Field04 != 0);

    IRenderPipeline* const Pipe = DALib::Pipeline;

    // 0x0055179d..0x005517e9: DAT_005e09c8 {state, value} walk, first state 0x8d.
    for (unsigned long const* Pair = kRsCloudsDraw; Pair[0] != 0; Pair += 2)
        Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));

    // 0x005517eb..0x00551889: RS 0x89 saved then 0; RS 0x16 saved then 1; RS 7 = 1; RS 0xe = 0;
    // RS 0x1b = 1.
    int Saved89 = 0;
    Pipe->GetRenderState(D3DRS_LIGHTING, &Saved89);
    Pipe->SetRenderState(D3DRS_LIGHTING, 0);
    int Saved16 = 0;
    Pipe->GetRenderState(D3DRS_CULLMODE, &Saved16);
    Pipe->SetRenderState(D3DRS_CULLMODE, 1);
    Pipe->SetRenderState(D3DRS_ZENABLE, 1);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);

    // 0x0055188f..0x005518c6: FUN_004215b0 identity rotation, zero translation, FUN_00422690(0, &m).
    {
        float Ident12[12];
        Win3DIdentityRotation3x3(Ident12);
        Ident12[9] = 0.0f;
        Ident12[10] = 0.0f;
        Ident12[11] = 0.0f;
        Win3DBindModelview(nullptr, Ident12);
    }

    // 0x005518cb..0x00551914: with alpha RS 0x13 = 5, RS 0x14 = 6; without, RS 0x13 = 2 and then
    // RS 0x13 = 4 (both pushes name state 0x13).
    if (bAlpha != 0)
    {
        Pipe->SetRenderState(D3DRS_SRCBLEND, 5);
        Pipe->SetRenderState(D3DRS_DESTBLEND, 6);
    }
    else
    {
        Pipe->SetRenderState(D3DRS_SRCBLEND, 2);
        Pipe->SetRenderState(D3DRS_SRCBLEND, 4);
    }

    // 0x0055191a..0x00551942: pipeline +0x108 (stage 0, FUN_00425640(first shape, 0)).
    Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(Shape0, 0));

    // 0x00551948..0x005519b8: DAT_005e09f0 on stage 0, DAT_005e0a28 on stage 1 (pipeline +0xf8),
    // each walked up to a zero state.
    for (unsigned long const* Pair = kTssClouds0; Pair[0] != 0; Pair += 2)
        Pipe->SetTextureStageState(0, static_cast<int>(Pair[0]), static_cast<int>(Pair[1]));
    for (unsigned long const* Pair = kTssClouds1; Pair[0] != 0; Pair += 2)
        Pipe->SetTextureStageState(1, static_cast<int>(Pair[0]), static_cast<int>(Pair[1]));

    // 0x005519ba..0x005519ed: RS 0x18 saved (into a zeroed local) then 5.
    int Saved18 = 0;
    Pipe->GetRenderState(D3DRS_ALPHAREF, &Saved18);
    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);

    // 0x005519f3..0x00551a17: V = I3DMathEngine::InverseRotate (+0x34)(CamXform, N).
    FVector V;
    CloudsMath()->InverseRotate(V, CamXform, *reinterpret_cast<FVector*>(N));

    // 0x00551a1a..0x00551a7b: P = V projected onto the view XY plane and normalised by
    // FUN_00507e70 of the float x*x + y*y. P.z is float(Inv * 0) (DAT_005c7474); the FPU keeps
    // that product and multiplies it by 0 again for the cross product below.
    float P[3];
    double PzZero = 0.0;
    {
        double const Inv = CloudsInvSqrt(static_cast<float>(
            static_cast<double>(V.X) * V.X + static_cast<double>(V.Y) * V.Y));
        P[0] = static_cast<float>(V.X * Inv);
        P[1] = static_cast<float>(V.Y * Inv);
        double const Pz = Inv * 0.0f;
        P[2] = static_cast<float>(Pz);
        PzZero = Pz * 0.0f;
    }

    // 0x00551a7b..0x00551aaa: Q = P x (0, 0, 1) with the zero terms kept (FUN_00422b80).
    float Q[3];
    Q[0] = static_cast<float>(P[1] - PzZero);
    Q[1] = static_cast<float>(PzZero - P[0]);
    Q[2] = static_cast<float>(static_cast<double>(P[0]) * 0.0f - static_cast<double>(P[1]) * 0.0f);

    // 0x00551aaf..0x00551b00: Q scaled by FUN_00507e70 of the float (y*y + z*z) + x*x.
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(Q[1]) * Q[1] + static_cast<double>(Q[2]) * Q[2]) +
            static_cast<double>(Q[0]) * Q[0]);
        double const Inv = CloudsInvSqrt(LenSq);
        Q[0] = static_cast<float>(Q[0] * Inv);
        Q[1] = static_cast<float>(Q[1] * Inv);
        Q[2] = static_cast<float>(Q[2] * Inv);
    }

    // 0x00551b02..0x00551b50: R^2, (0.7 R)^2, float(1 / (R^2 - (0.7 R)^2)) and
    // float(1 / (+0xe8 - +0xe4)) (DAT_005c75dc = 1.0f).
    float const RadiusSq = static_cast<float>(static_cast<double>(Radius) * Radius);
    float const InnerSq = static_cast<float>(static_cast<double>(InnerRadius) * InnerRadius);
    float const InvRing =
        static_cast<float>(1.0f / (static_cast<double>(RadiusSq) - InnerSq));
    float const NearLo = *reinterpret_cast<float*>(B + 0xe4);
    float const NearHi = *reinterpret_cast<float*>(B + 0xe8);
    float const InvNear = static_cast<float>(1.0f / (static_cast<double>(NearHi) - NearLo));

    FVertexMemoryLock Desc = {}; // [ESP+0xa0]: VB wrapper, FVF, start vertex, bits
    unsigned char** const Table = *reinterpret_cast<unsigned char***>(B);
    // 0x00551b2e..0x00551b57 and 0x005522d8..0x005522f1: the count +0xc is re-read at every
    // iteration's test; the table pointer is read once and stepped.
    for (int I = 0; I < *reinterpret_cast<int*>(B + 0xc); ++I)
    {
        // 0x00551b68..0x00551b8e: alpha float(+0xb4 * +0xe0); the puff must have +0x50 set and
        // +0x51 clear.
        float Alpha = static_cast<float>(static_cast<double>(*reinterpret_cast<float*>(B + 0xb4)) *
                                         *reinterpret_cast<float*>(B + 0xe0));
        unsigned char* const Puff = Table[I];
        if (Puff[0x50] == 0 || Puff[0x51] != 0)
            continue;

        // 0x00551b94..0x00551bec: VP = I3DMathEngine::InverseTransform (+0x3c)(CamXform, puff
        // position); puff +0x54 = -VP.z.
        FVector VP;
        CloudsMath()->InverseTransform(VP, CamXform, *reinterpret_cast<FVector*>(Puff));
        double const Depth = -static_cast<double>(VP.Z);
        *reinterpret_cast<float*>(Puff + 0x54) = static_cast<float>(Depth);

        // 0x00551bef..0x00551bfa: TEST AH,5 / JNP -> skipped only when ordered less than +0xe4.
        if (Depth < NearLo)
            continue;
        // 0x00551c00..0x00551c0c: TEST AH,0x41 / JNE -> the sphere fade runs only when strictly
        // greater than the ahead threshold.
        if (Depth > AheadThreshold)
        {
            // 0x00551c12..0x00551c85: D = (0, 0, -(+0xb8)) - VP (FUN_00423b60); its squared
            // length (z*z + y*y) + x*x, kept on the FPU and also stored as a float.
            float T[3];
            T[0] = 0.0f;
            T[1] = 0.0f;
            T[2] = -*reinterpret_cast<float*>(B + 0xb8);
            float D[3];
            Win3DVec3Sub(D, T, &VP.X);
            double const Dist2 = (static_cast<double>(D[2]) * D[2] + static_cast<double>(D[1]) * D[1]) +
                                 static_cast<double>(D[0]) * D[0];
            float const Dist2f = static_cast<float>(Dist2);
            // 0x00551c89..0x00551c92: TEST AH,0x41 / JE -> skipped when strictly beyond R^2.
            if (Dist2 > RadiusSq)
                continue;
            // 0x00551c98..0x00551cbd: past (0.7 R)^2 (strictly greater) the alpha ramps down.
            if (Dist2f > InnerSq)
                Alpha = static_cast<float>(((static_cast<double>(RadiusSq) - Dist2f) * Alpha) * InvRing);
        }
        // 0x00551cc1..0x00551cd0: ordered less than 0.02 (DAT_005e0e08, double) -> skipped.
        if (Alpha < 0.02)
            continue;

        // 0x00551cd6..0x00551d11: Wisp = (+0x90 * puff +0x3c) * +0xdc (FUN_00411d60 twice).
        float Wisp1[3];
        ScaleVec3(Wisp1, reinterpret_cast<float*>(B + 0x90), *reinterpret_cast<float*>(Puff + 0x3c));
        float Wisp2[3];
        ScaleVec3(Wisp2, Wisp1, *reinterpret_cast<float*>(B + 0xdc));

        // 0x00551d16..0x00551d3d: the size puff +0x18; alpha2 = alpha; TEST AH,0x41 / JNP ->
        // skipped when puff +0x54 is less than or equal to +0xe4.
        float const Size = *reinterpret_cast<float*>(Puff + 0x18);
        float Alpha2 = Alpha;
        float const PuffDepth = *reinterpret_cast<float*>(Puff + 0x54);
        if (PuffDepth <= NearLo)
            continue;
        // 0x00551d43..0x00551d67: TEST AH,5 / JP -> the near ramp runs only when ordered less than
        // +0xe8.
        if (PuffDepth < NearHi)
            Alpha2 = static_cast<float>(((static_cast<double>(PuffDepth) - NearLo) * Alpha) * InvNear);
        // 0x00551d6b..0x00551d7a: ordered less than 0.02 -> skipped.
        if (Alpha2 < 0.02)
            continue;

        // 0x00551d80..0x00551d9b: the colour puff +0xc..+0x14.
        float Col[3];
        Col[0] = *reinterpret_cast<float*>(Puff + 0xc);
        Col[1] = *reinterpret_cast<float*>(Puff + 0x10);
        Col[2] = *reinterpret_cast<float*>(Puff + 0x14);

        // 0x00551d90..0x00551e18: with +0x124 zero, +0xdc strictly positive (TEST AH,0x41 / JNE)
        // and puff +0x38 not less than 0 (TEST AH,1 / JNE skips on less or unordered), the wisp
        // is added (FUN_004d7270) and each channel not ordered-less than 1 is set to 1.0f.
        if (*reinterpret_cast<int*>(B + 0x124) == 0 && *reinterpret_cast<float*>(B + 0xdc) > 0.0f &&
            *reinterpret_cast<float*>(Puff + 0x38) >= 0.0f)
        {
            Vec3AddInPlace(Col, Wisp2);
            if (!(Col[0] < 1.0f))
                Col[0] = 1.0f;
            if (!(Col[1] < 1.0f))
                Col[1] = 1.0f;
            if (!(Col[2] < 1.0f))
                Col[2] = 1.0f;
        }

        // 0x00551e20..0x00551ef9: the diffuse word through _ftol (DAT_005c75d8 = 255.0f). With
        // alpha: A from alpha2, R G B from the colour. Without: A = 0xff and each channel is
        // colour * alpha2. Only the low byte of each _ftol survives the shifts and masks.
        unsigned Diffuse = 0;
        if (bAlpha != 0)
        {
            unsigned const R = CloudsFtol(static_cast<double>(Col[0]) * 255.0f);
            unsigned const A = CloudsFtol(static_cast<double>(Alpha2) * 255.0f);
            unsigned Word = ((R & 0xffu) | (A << 8)) << 8;
            unsigned const G = CloudsFtol(static_cast<double>(Col[1]) * 255.0f);
            Word = (Word | (G & 0xffu)) << 8;
            unsigned const Bl = CloudsFtol(static_cast<double>(Col[2]) * 255.0f);
            Diffuse = Word | (Bl & 0xffu);
        }
        else
        {
            unsigned const R = CloudsFtol((static_cast<double>(Col[0]) * Alpha2) * 255.0f);
            unsigned Word = (R | 0xffffff00u) << 8;
            unsigned const G = CloudsFtol((static_cast<double>(Col[1]) * Alpha2) * 255.0f);
            Word = (Word | (G & 0xffu)) << 8;
            unsigned const Bl = CloudsFtol((static_cast<double>(Col[2]) * Alpha2) * 255.0f);
            Diffuse = Word | (Bl & 0xffu);
        }

        // 0x00551efb..0x00552010: the first puff that gets here locks VertexBuffer (+0x14)
        // (FVF 0x142, +0xc * 6 vertices). The flag is set before the call, so a failed lock is
        // never retried and the cursors keep their null pointers. On success the cursors point
        // into the lock at the offsets and the stride the returned FVF gives (FUN_00552440
        // position, 0xc for D3DFVF_NORMAL, FUN_00552480 diffuse/specular, FUN_004061a0
        // texcoords).
        if (bLocked == 0)
        {
            bLocked = 1;
            if (DALib::VertexBuffer->LockVertexMemory(0x142, static_cast<unsigned int>(VertTotal),
                                                       &Desc) == GR_OK)
            {
                unsigned const Fvf = static_cast<unsigned>(Desc.Listing.Fvf);
                unsigned char* const Base = (unsigned char*)Desc.Vertices;
                int const NormalBytes = (Fvf & 0x10) != 0 ? 0xc : 0;
                int const DiffBytes = FvfDiffuseNormalBytes(static_cast<unsigned char>(Fvf));
                int const PosBytes = FvfPositionBytes(static_cast<unsigned char>(Fvf)) + NormalBytes;
                int const Stride = FvfTexcoordBlockBytes(Fvf) + (PosBytes + DiffBytes);
                PosCursor.Ptr = Base;
                PosCursor.Stride = Stride;
                DiffCursor.Ptr = Base + (FvfPositionBytes(static_cast<unsigned char>(Fvf)) + NormalBytes);
                DiffCursor.Stride = Stride;
                UvCursor.Ptr = Base + (FvfPositionBytes(static_cast<unsigned char>(Fvf)) +
                                       (NormalBytes + FvfDiffuseNormalBytes(static_cast<unsigned char>(Fvf))));
                UvCursor.Stride = Stride;
            }
        }

        // 0x00552014..0x00552021: shape +0x28[puff +0x1c]; a null shape skips the puff.
        FTexShape const* const Shape =
            *reinterpret_cast<FTexShape**>(B + 0x28 + *reinterpret_cast<int*>(Puff + 0x1c) * 4);
        if (Shape == nullptr)
            continue;

        // 0x00552027..0x00552141: SP = P * size, SQ = Q * size (FUN_00411d60);
        // C0 = (VP - SQ) - SP, C1 = (VP + SQ) - SP, C2 = (VP + SQ) + SP, C3 = (VP - SQ) + SP
        // (FUN_00423b60 / FUN_00423b30).
        float SP[3];
        ScaleVec3(SP, P, Size);
        float SQ[3];
        ScaleVec3(SQ, Q, Size);
        float Tmp[3];
        float C0[3];
        Win3DVec3Sub(Tmp, &VP.X, SQ);
        Win3DVec3Sub(C0, Tmp, SP);
        float Cc1[3];
        Win3DVec3Add(Tmp, &VP.X, SQ);
        Win3DVec3Sub(Cc1, Tmp, SP);
        float Cc2[3];
        Win3DVec3Add(Tmp, &VP.X, SQ);
        Win3DVec3Add(Cc2, Tmp, SP);
        float Cc3[3];
        Win3DVec3Sub(Tmp, &VP.X, SQ);
        Win3DVec3Add(Cc3, Tmp, SP);

        // 0x00552146..0x005522ad: six vertices through the cursors -- C0 (U0,V0), C1 (U1,V0),
        // C2 (U1,V1), C2 (U1,V1), C3 (U0,V1), C0 (U0,V0) -- all with the one diffuse word.
        float const* const Pos[6] = {C0, Cc1, Cc2, Cc2, Cc3, C0};
        float const Us[6] = {Shape->U0, Shape->U1, Shape->U1, Shape->U1, Shape->U0, Shape->U0};
        float const Vs[6] = {Shape->V0, Shape->V0, Shape->V1, Shape->V1, Shape->V1, Shape->V0};
        for (int Vert = 0; Vert < 6; ++Vert)
        {
            float* const OutPos = reinterpret_cast<float*>(PosCursor.Ptr);
            OutPos[0] = Pos[Vert][0];
            OutPos[1] = Pos[Vert][1];
            OutPos[2] = Pos[Vert][2];
            *reinterpret_cast<unsigned*>(DiffCursor.Ptr) = Diffuse;
            float* const OutUv = reinterpret_cast<float*>(UvCursor.Ptr);
            OutUv[0] = Us[Vert];
            OutUv[1] = Vs[Vert];
            PosCursor.Ptr += PosCursor.Stride;
            DiffCursor.Ptr += DiffCursor.Stride;
            UvCursor.Ptr += UvCursor.Stride;
        }

        // 0x005522af..0x005522cf: DAT_00679864 += 1; the vertex counter += 6.
        ++G_CloudsPuffsDrawn;
        VertCount += 6;
    }

    // 0x005522f7..0x00552337: after a lock (even a failed one) VertexBuffer +0x18 unlocks and
    // pipeline +0x11c draws a triangle list of the written vertex count from the lock's start.
    if (bLocked != 0)
    {
        DALib::VertexBuffer->UnlockVertexMemory(&Desc);
        Pipe->DrawPrimitiveVb(4, Desc.Listing.VertexBuffer, static_cast<unsigned int>(Desc.Listing.BaseVertex),
                              static_cast<unsigned int>(VertCount), nullptr);
    }

    // 0x0055233d..0x005523f0: RS 0x19 = 5; RS 0x18, 0x89, 0x16 restored; DAT_005e09e0 walk
    // (first state 0x8d); RS 0xe = 1; RS 0x1b = 0.
    Pipe->SetRenderState(D3DRS_ALPHAFUNC, 5);
    Pipe->SetRenderState(D3DRS_ALPHAREF, Saved18);
    Pipe->SetRenderState(D3DRS_LIGHTING, Saved89);
    Pipe->SetRenderState(D3DRS_CULLMODE, Saved16);
    for (unsigned long const* Pair = kRsCloudsRestore; Pair[0] != 0; Pair += 2)
        Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
}

// -------------------------------------------------------------------- LAB_005091a0

namespace
{
    // DAT_00675004 -- zeroed by LAB_005091a0 at 0x0050924a; its only reference (write-only).
    int G_NebulaCloudsPassWord = 0;
    // DAT_0067500c -- .bss dword whose only reference is the read at 0x005092f1. Nothing in
    // retail writes it, so the back-buffer lock below never runs; kept with its test as written.
    int G_NebulaCloudsLockBackBuffer = 0;
}

// .text:005091a0..00509330 (__thiscall, RET 4). No null tests on This, the list, a node's clouds,
// the Pipeline or the MaterialBatcher.
void NebulaDrawCloudsPanels(FNebulaEffect* Nebula, void* Camera)
{
    // 0x005091a6: byte +0x2c0.
    if (Nebula->FlagLoaded == 0)
        return;
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);

    // 0x005091b4..0x00509220: MaterialBatcher +0x20 (1, -1); RS 0x89 = 0; FUN_004101e0 (every
    // registered light off); RS 0x8b saved into a zeroed local, then -1; FUN_00422190(0).
    DALib::MaterialBatcher->SetBinOrder(1u, -1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_LIGHTING, 0);
    EngineLightClearRegisteredList();
    unsigned long Saved8b = 0;
    PipelineGetRenderState(DALib::Pipeline, D3DRS_AMBIENT, &Saved8b);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_AMBIENT, static_cast<unsigned long>(-1));
    Win3DSetDepthWrite(0);

    // 0x00509225..0x0050925b: FUN_00422440(&+0x2a0, +0x2ac, +0x2b0); DAT_00675004 = 0; RS 0x16 = 1.
    FogApplyColorAndRange(reinterpret_cast<float const*>(B + 0x2a0),
                          *reinterpret_cast<float*>(B + 0x2ac),
                          *reinterpret_cast<float*>(B + 0x2b0));
    G_NebulaCloudsPassWord = 0;
    PipelineSetRenderState(DALib::Pipeline, D3DRS_CULLMODE, 1);

    // 0x00509261..0x00509285: every node of the list at +0xf8 -> FUN_00551500 on node+8 with the
    // camera and +0x34.
    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        TexturePanelDrawClouds(N->Clouds, Camera, Nebula->ExclusionMgr);

    // 0x00509287..0x005092ec: MaterialBatcher (1, 1); RS 0x16 = 2; RS 0x89 = 1; RS 0x8b restored;
    // FUN_00422190(1); FUN_00421ed0(0xb60).
    DALib::MaterialBatcher->SetBinOrder(1u, 1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_CULLMODE, 2);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_LIGHTING, 1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_AMBIENT, Saved8b);
    Win3DSetDepthWrite(1);
    Win3DDisable(0xb60);

    // 0x005092f1..0x00509329: with DAT_0067500c set, a 5-dword lock descriptor whose +0x10 is
    // seeded with FUN_005b7010(0), Pipeline LockBuffer (slot +0x44) on it, then UnlockBuffer
    // (slot +0x48).
    if (G_NebulaCloudsLockBackBuffer != 0)
    {
        unsigned long LockDesc[5];
        LockDesc[4] = static_cast<unsigned long>(PixelFormatFromD3D(0));
        DALib::Pipeline->LockBuffer(LockDesc);
        DALib::Pipeline->UnlockBuffer();
    }
}

unsigned char TexturePanelsFormatHasAlpha(int FormatId)
{
    return TextureFormatHasAlpha(FormatId);
}

int TexturePanelsFvfPositionBytes(unsigned char FvfLow)
{
    return FvfPositionBytes(FvfLow);
}

int TexturePanelsFvfDiffuseNormalBytes(unsigned char FvfLow)
{
    return FvfDiffuseNormalBytes(FvfLow);
}
