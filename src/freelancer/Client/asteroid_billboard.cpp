// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Asteroid-field billboards -- retail Client\Space\SpaceTerrainRenderComp.cpp.
// Listing: LAB_0054dad0 / FUN_0054daf0 / LAB_0054db50 / FUN_0054db70 (the batch object),
// FUN_0054e130 (lighting), FUN_0054e900 (draw), FUN_0054f800, FUN_0054f830, FUN_0054f960,
// FUN_0054f9c0, FUN_0054f9f0, FUN_0054fd30, FUN_00552b10.

#include "asteroid_billboard.h"

#include "engine_light.h"
#include "game_options.h"
#include "part_cache.h"
#include "space_camera.h"
#include "space_scene_obj_draw.h"
#include "texture_shapes.h"

#include "DALib.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"

#include <math.h>
#include <string.h>

namespace
{
    char const kSpaceTerrainRenderCompCpp[] =
        "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp";

    // DAT_006140a8 / DAT_006140ac (.data, read only, 5 and 15 read sites in FUN_0054e130): the
    // length of the side offset added to Forward, and the wrap term of the light weight.
    float const kLightSideOffset = 1.0f;
    float const kLightWrap = 0.5f;

    // {state, value} tables, zero-terminated (FUN_0054f800 and its inlined copies), values read
    // out of the retail .rdata.
    // DAT_005e09f0 -- texture stage 0: COLOROP MODULATE, COLORARG1 TEXTURE, COLORARG2 DIFFUSE,
    // ALPHAOP MODULATE, ALPHAARG1 TEXTURE, ALPHAARG2 DIFFUSE.
    unsigned const kStage0States[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0, 0};
    // DAT_005e0a28 -- texture stage 1: COLOROP DISABLE, COLORARG1 TEXTURE, COLORARG2 CURRENT,
    // ALPHAOP DISABLE, ALPHAARG1 TEXTURE, ALPHAARG2 CURRENT.
    unsigned const kStage1States[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0, 0};
    // DAT_005e09c8 -- COLORVERTEX (0x8d) 1, DIFFUSEMATERIALSOURCE (0x91) COLOR1.
    unsigned const kRenderStatesMaterial[] = {0x8d, 1, 0x91, 1, 0, 0};
    // DAT_005e0978 -- LIGHTING (0x89) 0, SPECULARENABLE (0x1d) 0, ALPHABLENDENABLE (0x1b) 0.
    unsigned const kRenderStatesOpaque[] = {0x89, 0, 0x1d, 0, 0x1b, 0, 0, 0};
    // DAT_005e0998 -- LIGHTING 0, SPECULARENABLE 0, ALPHABLENDENABLE 1, SRCBLEND (0x13)
    // SRCALPHA, DESTBLEND (0x14) INVSRCALPHA.
    unsigned const kRenderStatesBlend[] = {0x89, 0, 0x1d, 0, 0x1b, 1, 0x13, 5, 0x14, 6, 0, 0};
    // DAT_005e09e0 -- COLORVERTEX 0.
    unsigned const kRenderStatesMaterialOff[] = {0x8d, 0, 0, 0};
    // DAT_005e0958 -- LIGHTING 1, SPECULARENABLE 0, ALPHABLENDENABLE 0 (through FUN_0054f800,
    // 0x0054f7cb).
    unsigned const kRenderStatesRestore[] = {0x89, 1, 0x1d, 0, 0x1b, 0, 0, 0};

    // The inlined SetTextureStageState walk (0x0054ece0 stage 0, 0x0054ed10 stage 1).
    void ApplyTextureStageStates(IRenderPipeline* Pipeline, int Stage, unsigned const* Table)
    {
        unsigned const* Entry = Table;
        while (Entry[0] != 0)
        {
            Pipeline->SetTextureStageState(Stage, static_cast<int>(Entry[0]),
                                           static_cast<int>(Entry[1]));
            Entry += 2;
        }
    }

    // FUN_00411350: Out = -In.
    void Negate3(float* Out, float const* In)
    {
        Out[0] = -In[0];
        Out[1] = -In[1];
        Out[2] = -In[2];
    }

    // FUN_00411d60 (cdecl Out, V, Scalar): three products, each stored as a float.
    void Scale3(float* Out, float const* V, float Scalar)
    {
        float const Z = Scalar * V[2];
        float const Y = Scalar * V[1];
        float const X = Scalar * V[0];
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_00423b30 (cdecl Out, A, B): Out = A + B.
    void Add3(float* Out, float const* A, float const* B)
    {
        float const Z = A[2] + B[2];
        float const Y = A[1] + B[1];
        float const X = A[0] + B[0];
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_004d7270 (Vector::operator+=): This[i] = Other[i] + This[i].
    void AddInPlace3(float* This, float const* Other)
    {
        This[0] = Other[0] + This[0];
        This[1] = Other[1] + This[1];
        This[2] = Other[2] + This[2];
    }

    // FUN_0041a390 (thiscall): (x*x + y*y) + z*z, FSQRT, 1.0f / length unrounded, each product
    // stored.
    void Normalise3(float* V)
    {
        double const Len = sqrt((static_cast<double>(V[0]) * V[0] +
                                 static_cast<double>(V[1]) * V[1]) +
                                static_cast<double>(V[2]) * V[2]);
        double const Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // FUN_004cbf40 (cdecl A, B): (a.z*b.z + a.y*b.y) + a.x*b.x, left on the FPU.
    double Dot3(float const* A, float const* B)
    {
        double const Sum = static_cast<double>(A[2]) * B[2] + static_cast<double>(A[1]) * B[1];
        return Sum + static_cast<double>(A[0]) * B[0];
    }

    // The inlined reciprocal square root of FUN_0054e900 (0x0054e9e0 / 0x0054eab5): magic
    // 0x5f3759df minus the float's bits shifted right (SAR), one Newton step
    // y * (1.5 - y*y*LenSq*0.5) (DAT_005ca220 = 1.5, DAT_005c75e0 = 0.5). The first site stores
    // the result as a float before using it (0x0054ea25), the second multiplies with the
    // unrounded value (0x0054eafc..0x0054eb0e) -- hence the double, and a name distinct from
    // common_misc.h's FastInvSqrt, which rounds its result to float.
    double BillboardInvSqrt(float LenSq)
    {
        int Bits = 0;
        memcpy(&Bits, &LenSq, sizeof(Bits));
        int const Guess = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &Guess, sizeof(Y));
        double const Step = 1.5 - static_cast<double>(Y) * Y * LenSq * 0.5;
        return Step * Y;
    }

    // FUN_0054e130's ambient/light weight: 0 below -Wrap (FCOMPP / TEST AH,5 / JP), otherwise
    // (Dot + Wrap) / (Wrap + 1.0), stored as a float.
    float LightWeight(double Dot)
    {
        if (Dot < -static_cast<double>(kLightWrap))
            return 0.0f;
        return static_cast<float>((Dot + kLightWrap) / (static_cast<double>(kLightWrap) + 1.0f));
    }

    // One of FUN_0054e130's five directions: normalise(Forward + Side * kLightSideOffset), its
    // weight against the light direction, then Out += Ambient and Out += Weight * Color.
    void AccumulateDirection(float* Out, float const* Forward, float const* Side,
                             float const* LightDir, float const* Ambient, float const* Color)
    {
        float Offset[3];
        Scale3(Offset, Side, kLightSideOffset);
        float Dir[3];
        Add3(Dir, Forward, Offset);
        Normalise3(Dir);
        float const Weight = LightWeight(Dot3(Dir, LightDir));
        AddInPlace3(Out, Ambient);
        float Lit[3];
        Scale3(Lit, Color, Weight);
        AddInPlace3(Out, Lit);
    }

    // The clamp FUN_0054e130 runs inline on four outputs (FUN_00521fa0's body, inlined):
    // FCOMP 1.0 / TEST AH,5 / JP -> 1.0 when not below 1.0 (unordered included). The fifth
    // goes through FUN_00521fa0 itself (CliAsteroidClampColorToOne).
    void ClampToOne3(float* V)
    {
        if (!(V[0] < 1.0f))
            V[0] = 1.0f;
        if (!(V[1] < 1.0f))
            V[1] = 1.0f;
        if (!(V[2] < 1.0f))
            V[2] = 1.0f;
    }

    // Colour pack of FUN_0054f9f0 (0x0054fa55..0x0054fab0 / 0x0054fc98..0x0054fcf9): ftol of
    // each component * 255 (DAT_005c75d8), alpha shifted up unmasked, the others masked.
    int Ftol255(float Value)
    {
        // FLD float / FMUL float 255 / CALL ftol: the product unrounded, then truncated.
        return static_cast<int>(static_cast<double>(Value) * 255.0);
    }

    unsigned PackColor(float Alpha, float const* Rgb)
    {
        unsigned Packed = static_cast<unsigned>(Ftol255(Alpha));
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[0])) & 0xffu);
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[1])) & 0xffu);
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[2])) & 0xffu);
        return Packed;
    }

    // Position / normal / colour sizes FUN_0054f830 derives from an FVF.
    int FvfPositionBytes(unsigned Fvf)
    {
        if ((Fvf & 0xcu) == 0xcu)
            return 0x1c;
        if ((Fvf & 4u) == 4u)
            return 0x10;
        if ((Fvf & 2u) == 2u)
            return 0xc;
        return 0;
    }

    int FvfNormalBytes(unsigned Fvf)
    {
        return (Fvf & 0x10u) != 0 ? 0xc : 0;
    }

    int FvfColorBytes(unsigned Fvf)
    {
        int Bytes = (Fvf & 0x40u) != 0 ? 4 : 0;
        if ((Fvf & 0x80u) != 0)
            Bytes += 4;
        return Bytes;
    }

    // The inlined "closing vertex" of FUN_0054e900 (0x0054f3a1..0x0054f42a and
    // 0x0054f685..0x0054f6fa): with flag bit 1, the saved first vertex written once more.
    void BatchCloseLoop(FAsteroidBillboardBatch& Batch)
    {
        if ((Batch.Flags & 2u) == 0)
            return;
        float* const Pos = reinterpret_cast<float*>(Batch.PosCursor.Ptr);
        Pos[0] = Batch.Saved[0].Pos[0];
        Pos[1] = Batch.Saved[0].Pos[1];
        Pos[2] = Batch.Saved[0].Pos[2];
        Batch.PosCursor.Advance(0);
        *reinterpret_cast<unsigned*>(Batch.ColorCursor.Ptr) = Batch.Saved[0].Color;
        Batch.ColorCursor.Advance(0);
        float* const Uv = reinterpret_cast<float*>(Batch.UvCursor.Ptr);
        Uv[0] = Batch.Saved[0].U;
        Uv[1] = Batch.Saved[0].V;
        Batch.UvCursor.Advance(0);
        ++Batch.Count;
    }

    // The inlined begin of FUN_0054e900 (0x0054ee4a..0x0054ee6e / 0x0054f4d6..0x0054f4ff):
    // flag bits 1 and 2 cleared, bit 0 set from bit 1 (the compiler's bitfield copy), the
    // triangle-list primitive.
    void BatchBeginTriangles(FAsteroidBillboardBatch& Batch)
    {
        unsigned char Flags = static_cast<unsigned char>(Batch.Flags & 0xf9u);
        unsigned char const Bit0 = static_cast<unsigned char>(((Flags >> 1) ^ Flags) & 1u);
        Flags = static_cast<unsigned char>(Flags ^ Bit0);
        Batch.Prim = 4;
        Batch.Flags = Flags;
    }

    // The inlined draw of FUN_0054e900 (0x0054f384..0x0054f456 / 0x0054f668..0x0054f726):
    // unlock, then with vertices written the closing vertex and DrawPrimitiveVb (slot 71).
    void BatchFlush(FAsteroidBillboardBatch& Batch)
    {
        Batch.VbMgr->UnlockVertexMemory(&Batch.Lock);
        if (Batch.Count == 0)
            return;
        BatchCloseLoop(Batch);
        Batch.Pipeline->DrawPrimitiveVb(Batch.Prim, Batch.Lock.Listing.VertexBuffer,
                                        static_cast<unsigned int>(Batch.Lock.Listing.BaseVertex),
                                        static_cast<unsigned int>(Batch.Count), nullptr);
    }

    // The two jump tables at 0x0054f7e0 / 0x0054f7f0: Index 0..3 direct, 4..7 minus 4,
    // anything else case 0.
    float const* PickCorner(int Index, float const* Corner0, float const* Corner1,
                            float const* Corner2, float const* Corner3)
    {
        if (Index > 3)
        {
            Index -= 4;
            if (static_cast<unsigned>(Index) > 3u)
                Index = 0;
        }
        switch (Index)
        {
        case 1:
            return Corner1;
        case 2:
            return Corner2;
        case 3:
            return Corner3;
        default:
            return Corner0;
        }
    }
}

// ------------------------------------------------------------------ FBillboardVertexCursor

FBillboardVertexCursor::FBillboardVertexCursor(int InStride)
    : Ptr(nullptr)
    , Stride(InStride)
{
}

FBillboardVertexCursor::~FBillboardVertexCursor()
{
    Ptr = nullptr;
    Stride = 0;
}

void FBillboardVertexCursor::Advance(int /*Unused*/)
{
    // FUN_00552b10.
    Ptr += Stride;
}

// ------------------------------------------------------------------ FAsteroidBillboardBatch

static_assert(sizeof(FBillboardVertexCursor) == 8, "cursor is {ptr, stride}");
static_assert(sizeof(FAsteroidBillboardBatch) == 0x84, "DAT_006797d0 object is 0x84 bytes");
static_assert(offsetof(FAsteroidBillboardBatch, Lock) == 0x14, "batch lock out +0x14");
static_assert(offsetof(FAsteroidBillboardBatch, Saved) == 0x28, "batch saved vertices +0x28");
static_assert(offsetof(FAsteroidBillboardBatch, Flags) == 0x58, "batch flags +0x58");
static_assert(offsetof(FAsteroidBillboardBatch, Color) == 0x68, "batch colour +0x68");
static_assert(offsetof(FAsteroidBillboardBatch, PosCursor) == 0x6c, "batch cursors +0x6c");

// LAB_0054dad0: FUN_0054daf0(0x142), atexit(LAB_0054db50).
FAsteroidBillboardBatch G_AsteroidBillboardBatch(0x142);

FAsteroidBillboardBatch::FAsteroidBillboardBatch(unsigned InFvf)
    : Prim(0x7fffffff)
    , Count(0)
    , Fvf(InFvf)
    , Pipeline(nullptr)
    , VbMgr(nullptr)
    , LockedCount(0)
    , Flags(0)
    , QuadCounter(0)
    , U(0.0f)
    , V(0.0f)
    , Color(0xffffffffu)
    , PosCursor(0xc)
    , ColorCursor(4)
    , UvCursor(8)
{
    // FUN_0054daf0: cursors {0, 0xc} / {0, 4} / {0, 8}, +0x08 = the FVF, +0x0c / +0x10 /
    // +0x24 / +0x04 / +0x5c zero, flags bits 0..2 cleared, colour -1, UV 0, primitive
    // 0x7fffffff. Lock and Saved are not written; the static object starts zeroed.
    memset(&Lock, 0, sizeof(Lock));
    memset(Saved, 0, sizeof(Saved));
    memset(Pad59, 0, sizeof(Pad59));
}

FAsteroidBillboardBatch::~FAsteroidBillboardBatch()
{
    // FUN_0054db70 (via the LAB_0054db50 once-guard DAT_006797c8): release +0x0c and +0x10,
    // then the three cursor destructors.
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    if (VbMgr != nullptr)
    {
        VbMgr->Release();
        VbMgr = nullptr;
    }
}

void FAsteroidBillboardBatch::SetInterfaces(IRenderPipeline* InPipeline,
                                            IVertexBufferManager* InVbMgr)
{
    // FUN_0054f960.
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    if (VbMgr != nullptr)
    {
        VbMgr->Release();
        VbMgr = nullptr;
    }
    if (InPipeline != nullptr)
    {
        Pipeline = InPipeline;
        InPipeline->AddRef();
    }
    if (InVbMgr != nullptr)
    {
        VbMgr = InVbMgr;
        InVbMgr->AddRef();
    }
}

void FAsteroidBillboardBatch::Reset()
{
    // FUN_0054f9c0.
    LockedCount = 0;
    Count = 0;
    QuadCounter = 0;
    Flags = static_cast<unsigned char>(Flags & 0xf8u);
    Color = 0xffffffffu;
    V = 0.0f;
    U = 0.0f;
    Prim = 0x7fffffff;
}

void FAsteroidBillboardBatch::BindCursors(unsigned char* Base, unsigned InFvf)
{
    // FUN_0054f830. The stride is sized from the lock's own FVF (+0x18, Lock.Listing.Fvf).
    unsigned const OwnFvf = static_cast<unsigned>(Lock.Listing.Fvf);
    int const VertexStride = FvfTexcoordBlockBytes(OwnFvf) + FvfColorBytes(OwnFvf) +
                             FvfNormalBytes(OwnFvf) + FvfPositionBytes(OwnFvf);

    PosCursor.Ptr = Base;
    PosCursor.Stride = VertexStride;

    ColorCursor.Ptr = Base + FvfNormalBytes(InFvf) + FvfPositionBytes(InFvf);
    ColorCursor.Stride = VertexStride;

    UvCursor.Ptr =
        Base + FvfColorBytes(InFvf) + FvfNormalBytes(InFvf) + FvfPositionBytes(InFvf);
    UvCursor.Stride = VertexStride;
}

void FAsteroidBillboardBatch::Vertex(float X, float Y, float Z)
{
    // FUN_0054fd30.
    ++Count;
    float* const Pos = reinterpret_cast<float*>(PosCursor.Ptr);
    Pos[0] = X;
    Pos[1] = Y;
    Pos[2] = Z;
    PosCursor.Ptr += PosCursor.Stride;
    *reinterpret_cast<unsigned*>(ColorCursor.Ptr) = Color;
    ColorCursor.Ptr += ColorCursor.Stride;
    float* const Uv = reinterpret_cast<float*>(UvCursor.Ptr);
    Uv[0] = U;
    Uv[1] = V;
    UvCursor.Ptr += UvCursor.Stride;

    // 0x0054fd7f: bit 0 -- keep this vertex as the loop's first.
    if ((Flags & 1u) != 0)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfeu);
        Saved[0].Color = Color;
        Saved[0].U = U;
        Saved[0].V = V;
        Saved[0].Pos[0] = X;
        Saved[0].Pos[1] = Y;
        Saved[0].Pos[2] = Z;
    }

    // 0x0054fdb2: bit 2 -- quads. Vertices 0 and 2 of each four are saved; after the fourth
    // both are written again, turning the quad into two triangles.
    if ((Flags & 4u) == 0)
        return;
    if ((QuadCounter & 1u) == 0)
    {
        FBillboardSavedVertex& Slot = Saved[QuadCounter >> 1];
        Slot.Color = Color;
        Slot.U = U;
        Slot.V = V;
        Slot.Pos[0] = X;
        Slot.Pos[1] = Y;
        Slot.Pos[2] = Z;
    }
    ++QuadCounter;
    if (QuadCounter < 4)
        return;
    QuadCounter = 0;
    int Index = 0;
    for (Index = 0; Index < 2; ++Index)
    {
        float* const OutPos = reinterpret_cast<float*>(PosCursor.Ptr);
        OutPos[0] = Saved[Index].Pos[0];
        OutPos[1] = Saved[Index].Pos[1];
        OutPos[2] = Saved[Index].Pos[2];
        PosCursor.Ptr += PosCursor.Stride;
        *reinterpret_cast<unsigned*>(ColorCursor.Ptr) = Saved[Index].Color;
        ColorCursor.Ptr += ColorCursor.Stride;
        float* const OutUv = reinterpret_cast<float*>(UvCursor.Ptr);
        OutUv[0] = Saved[Index].U;
        OutUv[1] = Saved[Index].V;
        UvCursor.Ptr += UvCursor.Stride;
    }
    Count += 2;
}

// ------------------------------------------------------------------ free functions

void BillboardApplyRenderStates(IRenderPipeline* Pipeline, unsigned const* Table)
{
    // FUN_0054f800.
    unsigned const* Entry = Table;
    while (Entry[0] != 0)
    {
        Pipeline->SetRenderState((D3DRENDERSTATETYPE)Entry[0], static_cast<int>(Entry[1]));
        Entry += 2;
    }
}

void BillboardComputeLighting(FAsteroidBillboardBlock* Bb, float const* Pos,
                              float const* Forward, float const* Up, float const* Right,
                              float* OutCenter, float* OutCorner0, float* OutCorner1,
                              float* OutCorner2, float* OutCorner3)
{
    // FUN_0054e130. 0x0054e141..0x0054e155: FUN_00537f40(Slots, 16, Pos, 100.0f, 0).
    void* Slots[16];
    int const LightCount = SpaceCollectNearbyLights(Slots, 0x10, Pos, 100.0f, 0);

    // 0x0054e15a..0x0054e1a0: D3DRS_AMBIENT bytes (FUN_00410280) as floats into OutCorner0.
    unsigned char Ambient[3];
    EngineLightGetAmbientRgbBytes(Ambient);
    OutCorner0[0] = static_cast<float>(Ambient[0]);
    OutCorner0[1] = static_cast<float>(Ambient[1]);
    OutCorner0[2] = static_cast<float>(Ambient[2]);

    // 0x0054e1a3..0x0054e1af: FUN_00507ea0 with ambient_intensity * 1/255 (DAT_005c7f68).
    float const AmbientScale = Bb->AmbientIntensity * 0.003921568859368563f;
    OutCorner0[0] = AmbientScale * OutCorner0[0];
    OutCorner0[1] = AmbientScale * OutCorner0[1];
    OutCorner0[2] = AmbientScale * OutCorner0[2];

    // 0x0054e1b4..0x0054e1ef: times the tinted ambient (+0x58), then (0x0054e1f2..0x0054e234)
    // times color_shift (+0x64), each through a FUN_00422b80 temporary.
    float const Ax = OutCorner0[0] * Bb->TintedAmbient0;
    float const Ay = OutCorner0[1] * Bb->TintedAmbient1;
    float const Az = OutCorner0[2] * Bb->TintedAmbient2;
    OutCorner0[0] = Ax;
    OutCorner0[1] = Ay;
    OutCorner0[2] = Az;
    float const Sx = OutCorner0[0] * Bb->ColorShift0;
    float const Sy = OutCorner0[1] * Bb->ColorShift1;
    float const Sz = OutCorner0[2] * Bb->ColorShift2;
    OutCorner0[0] = Sx;
    OutCorner0[1] = Sy;
    OutCorner0[2] = Sz;

    // 0x0054e237..0x0054e273: the same three floats into the other four outputs.
    int Axis = 0;
    for (Axis = 0; Axis < 3; ++Axis)
    {
        OutCorner3[Axis] = OutCorner0[Axis];
        OutCorner2[Axis] = OutCorner0[Axis];
        OutCorner1[Axis] = OutCorner0[Axis];
        OutCenter[Axis] = OutCorner0[Axis];
    }

    // 0x0054e290..0x0054e772. Retail's direction and colour locals are not initialised: a light
    // that FUN_00410960 rejects (not enabled) leaves the previous light's values -- or stack
    // garbage for the first one -- in them. They start at zero here.
    float LightDir[3] = {0.0f, 0.0f, 0.0f};
    float LightColor[3] = {0.0f, 0.0f, 0.0f};
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    int Lit = 0;
    int SlotIndex = 0;
    for (SlotIndex = 0; SlotIndex < LightCount; ++SlotIndex)
    {
        // 0x0054e294: CMP Lit, MaxLights (+0x48) / JGE -> done.
        if (Lit >= Bb->MaxLights)
            break;
        FEngineLight* const Light = static_cast<FEngineLight*>(Slots[SlotIndex]);
        // 0x0054e2a4: only directional lights (+0xb0 == 3).
        if (Light->Type != 3)
            continue;

        // 0x0054e2b1..0x0054e2e0: a zeroed ambient accumulator, FUN_00410960.
        float LightAmbient[3] = {0.0f, 0.0f, 0.0f};
        EngineLightSample(Light, Pos, LightDir, LightAmbient, LightColor);

        // 0x0054e2e5..0x0054e30c: the colour times the tinted diffuse (+0x4c).
        float const Cx = LightColor[0] * Bb->TintedDiffuse0;
        float const Cy = LightColor[1] * Bb->TintedDiffuse1;
        float const Cz = LightColor[2] * Bb->TintedDiffuse2;
        LightColor[0] = Cx;
        LightColor[1] = Cy;
        LightColor[2] = Cz;

        // 0x0054e311..0x0054e3e3: Forward + Right -> corner 0.
        AccumulateDirection(OutCorner0, Forward, Right, LightDir, LightAmbient, LightColor);
        // 0x0054e3e8..0x0054e4bc: Forward - Right -> corner 1.
        float NegRight[3];
        Negate3(NegRight, Right);
        AccumulateDirection(OutCorner1, Forward, NegRight, LightDir, LightAmbient, LightColor);
        // 0x0054e4c1..0x0054e582: Forward + Up -> corner 2.
        AccumulateDirection(OutCorner2, Forward, Up, LightDir, LightAmbient, LightColor);
        // 0x0054e587..0x0054e665: Forward - Up -> corner 3.
        float NegUp[3];
        Negate3(NegUp, Up);
        AccumulateDirection(OutCorner3, Forward, NegUp, LightDir, LightAmbient, LightColor);
        // 0x0054e66a..0x0054e757: Forward + 0 -> the centre (the last add written inline).
        AccumulateDirection(OutCenter, Forward, Zero, LightDir, LightAmbient, LightColor);

        ++Lit;
    }

    // 0x0054e778..0x0054e8f1: every output clamped to 1.0 (the centre through FUN_00521fa0).
    ClampToOne3(OutCorner0);
    ClampToOne3(OutCorner1);
    ClampToOne3(OutCorner2);
    ClampToOne3(OutCorner3);
    CliAsteroidClampColorToOne(OutCenter); // 0x0054e8f1: FUN_00521fa0
}

void BillboardEmitRock(FAsteroidBillboardBlock* /*Bb*/, FAsteroidBillboardRock* Rock,
                       float const* Right, float const* Up, float const* CornerColor,
                       float const* CenterColor)
{
    // FUN_0054f9f0. 0x0054f9fd..0x0054fa50: Size = HalfSize * 2; Right and Up scaled by it.
    float const Size = Rock->HalfSize + Rock->HalfSize;
    float RightScaled[3];
    RightScaled[0] = Right[0] * Size;
    RightScaled[1] = Right[1] * Size;
    RightScaled[2] = Right[2] * Size;
    float UpScaled[3];
    UpScaled[0] = Up[0] * Size;
    UpScaled[1] = Up[1] * Size;
    UpScaled[2] = Up[2] * Size;

    FAsteroidBillboardBatch& Batch = G_AsteroidBillboardBatch;

    // 0x0054fa55..0x0054fab0: the base colour.
    Batch.Color = PackColor(Rock->Alpha, CornerColor);

    // 0x0054fab6..0x0054fb28: the base UV pair from FlipU + 2 * FlipV.
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 0.0f;
    float V1 = 0.0f;
    int const Corner = static_cast<int>(Rock->bFlipU) + static_cast<int>(Rock->bFlipV) * 2;
    if (Corner == 0)
    {
        // 0x0054fb10 then 0x0054fb20.
        V0 = 0.0f;
        U1 = 1.0f;
        U0 = 0.0f;
        V1 = 0.0f;
    }
    else if (Corner == 1)
    {
        // 0x0054faee.
        U0 = 1.0f;
        V0 = 0.0f;
        U1 = 1.0f;
        V1 = 1.0f;
    }
    else
    {
        // 0x0054fac9: U1 = 0, V0 = 1; Corner 2 then U0 = 1, V1 = 1, anything else falls into
        // 0x0054fb20: U0 = 0, V1 = 0.
        U1 = 0.0f;
        V0 = 1.0f;
        if (Corner == 2)
        {
            U0 = 1.0f;
            V1 = 1.0f;
        }
        else
        {
            U0 = 0.0f;
            V1 = 0.0f;
        }
    }

    // 0x0054fb30..0x0054fb8e: A spans the base, B reaches the apex.
    float A[3];
    float B[3];
    if (Rock->bSwapAxes != 0)
    {
        A[0] = RightScaled[0];
        A[1] = RightScaled[1];
        A[2] = RightScaled[2];
        B[0] = UpScaled[0];
        B[1] = UpScaled[1];
        B[2] = UpScaled[2];
    }
    else
    {
        A[0] = UpScaled[0];
        A[1] = UpScaled[1];
        A[2] = UpScaled[2];
        B[0] = RightScaled[0];
        B[1] = RightScaled[1];
        B[2] = RightScaled[2];
    }
    // 0x0054fb85..0x0054fbba: B times -1.0 (DAT_005c7500).
    if (Rock->bNegateApex != 0)
    {
        B[0] = B[0] * -1.0f;
        B[1] = B[1] * -1.0f;
        B[2] = B[2] * -1.0f;
    }

    // 0x0054fbbe..0x0054fc3a: P - A, P + A, P + B.
    float const BaseLow[3] = {Rock->Pos[0] - A[0], Rock->Pos[1] - A[1], Rock->Pos[2] - A[2]};
    float const BaseHigh[3] = {A[0] + Rock->Pos[0], A[1] + Rock->Pos[1], A[2] + Rock->Pos[2]};
    float const Apex[3] = {B[0] + Rock->Pos[0], B[1] + Rock->Pos[1], B[2] + Rock->Pos[2]};

    // 0x0054fc3f..0x0054fc93: the base pair at (U0, V0) and (U1, V1).
    Batch.U = U0;
    Batch.V = V0;
    Batch.Vertex(BaseLow[0], BaseLow[1], BaseLow[2]);
    Batch.U = U1;
    Batch.V = V1;
    Batch.Vertex(BaseHigh[0], BaseHigh[1], BaseHigh[2]);

    // 0x0054fc98..0x0054fd21: the apex in the centre colour at (0.5, 0.5).
    Batch.Color = PackColor(Rock->Alpha, CenterColor);
    Batch.U = 0.5f;
    Batch.V = 0.5f;
    Batch.Vertex(Apex[0], Apex[1], Apex[2]);
}

void BillboardDraw(FAsteroidBillboardBlock* Bb, FSpaceCamera* Camera,
                   Exclusion::AsteroidExclusionZoneMgr* Excl, int ExcludeType)
{
    // FUN_0054e900.
    ICamera* const Cam = AsCamera(Camera);

    // 0x0054e910..0x0054e92b: the camera position (slot 3), copied.
    float const* const CamPosSource = Cam->GetPosition();
    float CamPos[3] = {CamPosSource[0], CamPosSource[1], CamPosSource[2]};

    // 0x0054e92f..0x0054e960: Forward = -(orientation column 2) (slot 4, elements 2, 5, 8).
    float const* const Orient = Cam->GetOrientation();
    float Forward[3];
    Forward[0] = -Orient[2];
    Forward[1] = -Orient[5];
    Forward[2] = -Orient[8];

    // 0x0054e964..0x0054e9a4: the centre of the rock sphere, Float04 ahead of the camera.
    float FarCenter[3];
    FarCenter[0] = static_cast<float>(static_cast<double>(Forward[0]) * Bb->Float04 + CamPos[0]);
    FarCenter[1] = static_cast<float>(static_cast<double>(Forward[1]) * Bb->Float04 + CamPos[1]);
    FarCenter[2] = static_cast<float>(static_cast<double>(Forward[2]) * Bb->Float04 + CamPos[2]);

    // 0x0054e9a9..0x0054ea43: Right = normalise(Forward x Up), reciprocal square root inline.
    float* const Up = Bb->Up;
    double const CrossX =
        static_cast<double>(Forward[1]) * Up[2] - static_cast<double>(Forward[2]) * Up[1];
    double const CrossY =
        static_cast<double>(Forward[2]) * Up[0] - static_cast<double>(Forward[0]) * Up[2];
    float const CrossZ = static_cast<float>(static_cast<double>(Forward[0]) * Up[1] -
                                            static_cast<double>(Forward[1]) * Up[0]);
    float const RightLenSq = static_cast<float>(
        (CrossX * CrossX + static_cast<double>(CrossZ) * CrossZ) + CrossY * CrossY);
    float const RightInv = static_cast<float>(BillboardInvSqrt(RightLenSq));
    float Right[3];
    Right[0] = static_cast<float>(CrossX * RightInv);
    Right[1] = static_cast<float>(CrossY * RightInv);
    Right[2] = CrossZ * RightInv;

    // 0x0054ea47..0x0054eb11: Up = normalise(Right x Forward), written back into the block.
    Up[0] = static_cast<float>(static_cast<double>(Right[1]) * Forward[2] -
                               static_cast<double>(Right[2]) * Forward[1]);
    Up[1] = static_cast<float>(static_cast<double>(Right[2]) * Forward[0] -
                               static_cast<double>(Forward[2]) * Right[0]);
    Up[2] = static_cast<float>(static_cast<double>(Forward[1]) * Right[0] -
                               static_cast<double>(Right[1]) * Forward[0]);
    float const UpLenSq = static_cast<float>(
        (static_cast<double>(Up[0]) * Up[0] + static_cast<double>(Up[1]) * Up[1]) +
        static_cast<double>(Up[2]) * Up[2]);
    double const UpInv = BillboardInvSqrt(UpLenSq);
    Up[0] = static_cast<float>(UpInv * Up[0]);
    Up[1] = static_cast<float>(UpInv * Up[1]);
    Up[2] = static_cast<float>(UpInv * Up[2]);

    // 0x0054eaf4..0x0054ebd2: the five colours -- lit (option LIGHTING, 0x18) or the plain
    // tinted diffuse (+0x4c).
    float CenterColor[3];
    float Corner0[3];
    float Corner1[3];
    float Corner2[3];
    float Corner3[3];
    if (GetGameOption(kGameOptLighting) != 0)
    {
        BillboardComputeLighting(Bb, CamPos, Forward, Bb->Up, Right, CenterColor, Corner0,
                                 Corner1, Corner2, Corner3);
    }
    else
    {
        float const Diffuse[3] = {Bb->TintedDiffuse0, Bb->TintedDiffuse1, Bb->TintedDiffuse2};
        int Axis = 0;
        for (Axis = 0; Axis < 3; ++Axis)
        {
            Corner3[Axis] = Diffuse[Axis];
            Corner2[Axis] = Diffuse[Axis];
            Corner1[Axis] = Diffuse[Axis];
            Corner0[Axis] = Diffuse[Axis];
            CenterColor[Axis] = Diffuse[Axis];
        }
    }

    // 0x0054ebd9: no shape, nothing drawn and no state touched.
    if (Bb->Shape == nullptr)
        return;

    // 0x0054ebe6..0x0054ec33: RadiusSq = Float00^2, the fade band Float2C: 1 / Fade^2 and
    // RadiusSq - Fade^2, all doubles. The camera byte +0xbc re-scatters every rock.
    double const RadiusSq = static_cast<double>(Bb->Float00) * Bb->Float00;
    unsigned char const bScatterAll = Camera->FlagBC;
    double const FadeSq = static_cast<double>(Bb->Float2C) * Bb->Float2C;
    double const InvFadeSq = 1.0 / FadeSq;
    double const InnerSq = RadiusSq - FadeSq;

    IRenderPipeline* const Pipe = DALib::Pipeline;
    // 0x0054ec35..0x0054ec42: SetModelview(camera slot 16).
    Pipe->SetModelview(Cam->GetViewTransform());
    // 0x0054ec45..0x0054ec8c: CULLMODE none, ALPHAREF saved and set to 0x80.
    Pipe->SetRenderState(D3DRS_CULLMODE, 1);
    int OldAlphaRef = 0;
    Pipe->GetRenderState(D3DRS_ALPHAREF, &OldAlphaRef);
    Pipe->SetRenderState(D3DRS_ALPHAREF, 0x80);
    // 0x0054ec92..0x0054ecc4: the shape's frame-0 texture on stage 0, TEXCOORDINDEX 0.
    Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(Bb->Shape, 0));
    Pipe->SetTextureStageState(0, 0xb, 0);
    ApplyTextureStageStates(Pipe, 0, kStage0States);
    ApplyTextureStageStates(Pipe, 1, kStage1States);
    BillboardApplyRenderStates(Pipe, kRenderStatesMaterial);
    BillboardApplyRenderStates(Pipe, kRenderStatesOpaque);

    // 0x0054ed89..0x0054edf0: FUN_0054f960 inlined, with DALib's pipeline and VB manager.
    FAsteroidBillboardBatch& Batch = G_AsteroidBillboardBatch;
    Batch.SetInterfaces(DALib::Pipeline, DALib::VertexBuffer);

    // 0x0054edf6..0x0054ee3a: FUN_0054f9c0 inlined; three vertices per rock.
    int Deferred = 0;
    int const VertexTotal = Bb->RockCount * 3;
    Batch.Reset();
    if (VertexTotal != 0)
    {
        BatchBeginTriangles(Batch);
        bool const bLocked = Batch.VbMgr->LockVertexMemory(
                                 Batch.Fvf, static_cast<unsigned int>(VertexTotal), &Batch.Lock) == 0;
        if (bLocked)
        {
            Batch.LockedCount = VertexTotal;
            Batch.BindCursors((unsigned char*)Batch.Lock.Vertices,
                              static_cast<unsigned>(Batch.Lock.Listing.Fvf));

            // 0x0054eea6..0x0054f37e: place, fade and draw the opaque rocks.
            int RockIndex = 0;
            for (RockIndex = 0; RockIndex < Bb->RockCount; ++RockIndex)
            {
                FAsteroidBillboardRock* const Rock = &Bb->Rocks[RockIndex];
                Vector const& RockPos = *reinterpret_cast<Vector const*>(Rock->Pos);
                double DistSq = 0.0;

                if (bScatterAll != 0)
                {
                    // 0x0054eecf..0x0054ef2b: anywhere inside the sphere.
                    Vector const Offset = random_inside_sphere(Bb->Float00);
                    Rock->Pos[0] = static_cast<float>(static_cast<double>(FarCenter[0]) + Offset.x);
                    Rock->Pos[1] = static_cast<float>(static_cast<double>(FarCenter[1]) + Offset.y);
                    Rock->Pos[2] = static_cast<float>(static_cast<double>(FarCenter[2]) + Offset.z);

                    // 0x0054ef2e..0x0054ef54: outside the zone (immersion exactly 0) -> hidden.
                    if (Bb->ZoneRef != nullptr && Bb->ZoneRef->GetImmersion(RockPos) == 0.0f)
                    {
                        Rock->bVisible = 0;
                        continue;
                    }
                    Rock->bVisible = 1;
                    if (Excl != nullptr)
                    {
                        // 0x0054ef65..0x0054efc5: is_excluded (slot 1) with a -1 depth.
                        float ExclusionDepth = -1.0f;
                        if (Excl->is_excluded(RockPos,
                                              static_cast<Exclusion::ExcludeObjectType>(ExcludeType),
                                              &ExclusionDepth))
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        if (ExclusionDepth > 0.0f)
                        {
                            if (Rock->Phase > ExclusionDepth)
                                Rock->bVisible = 0;
                        }
                        else
                        {
                            if (Rock->Phase > Bb->DensityScale)
                                Rock->bVisible = 0;
                        }
                    }

                    // 0x0054efc9..0x0054eff6: squared distance to the sphere centre.
                    double const Dx = static_cast<double>(Rock->Pos[0]) - FarCenter[0];
                    double const Dy = static_cast<double>(Rock->Pos[1]) - FarCenter[1];
                    double const Dz = static_cast<double>(Rock->Pos[2]) - FarCenter[2];
                    DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
                }
                else
                {
                    // 0x0054f0a8..0x0054f0ed: squared distance; within RadiusSq + 10 (DAT_005c9ce8)
                    // the rock stays.
                    double const Dx = static_cast<double>(Rock->Pos[0]) - FarCenter[0];
                    double const Dy = static_cast<double>(Rock->Pos[1]) - FarCenter[1];
                    double const Dz = static_cast<double>(Rock->Pos[2]) - FarCenter[2];
                    DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
                    if (DistSq > RadiusSq + 10.0)
                    {
                        // 0x0054f0f3..0x0054f17a: respawned on the sphere's surface, invisible
                        // (alpha 0) for this frame.
                        Rock->bVisible = 1;
                        Vector const Dir = random_on_unit_sphere();
                        float const Radius = Bb->Float00;
                        float const OffsetZ = static_cast<float>(static_cast<double>(Radius) * Dir.z);
                        Rock->Pos[0] = static_cast<float>(static_cast<double>(Radius) * Dir.x + FarCenter[0]);
                        Rock->Pos[1] = static_cast<float>(static_cast<double>(Radius) * Dir.y + FarCenter[1]);
                        Rock->Pos[2] = static_cast<float>(static_cast<double>(OffsetZ) + FarCenter[2]);
                        Rock->Alpha = 0.0f;

                        if (Bb->ZoneRef != nullptr && Bb->ZoneRef->GetImmersion(RockPos) == 0.0f)
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        // 0x0054f1ad: without an exclusion manager nothing more this frame.
                        if (Excl == nullptr)
                            continue;
                        float ExclusionDepth = -1.0f;
                        if (Excl->is_excluded(RockPos,
                                              static_cast<Exclusion::ExcludeObjectType>(ExcludeType),
                                              &ExclusionDepth))
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        if (ExclusionDepth > 0.0f)
                        {
                            if (Rock->Phase > ExclusionDepth)
                                Rock->bVisible = 0;
                        }
                        else
                        {
                            if (Rock->Phase > Bb->DensityScale)
                                Rock->bVisible = 0;
                        }
                        continue;
                    }
                }

                // 0x0054eff8: hidden rocks are skipped.
                if (Rock->bVisible == 0)
                    continue;

                // 0x0054f003..0x0054f02b: depth along Forward, stored as a float.
                double const DepthD =
                    (static_cast<double>(Rock->Pos[0] - CamPos[0]) * Forward[0] +
                     static_cast<double>(Rock->Pos[2] - CamPos[2]) * Forward[2]) +
                    static_cast<double>(Rock->Pos[1] - CamPos[1]) * Forward[1];
                float const Depth = static_cast<float>(DepthD);

                if (DepthD > Bb->Float04)
                {
                    // 0x0054f03d..0x0054f0a0: beyond the sphere centre the fade band narrows
                    // with depth: T = 1 - (R - (Depth - Float04)) / R, capped at 1;
                    // Band = ((1 - pct) * T * 0.5 + pct) * R.
                    double T = 1.0 - (static_cast<double>(Bb->Float00) -
                                      (static_cast<double>(Depth) - Bb->Float04)) /
                                         Bb->Float00;
                    if (T > 1.0)
                        T = 1.0;
                    double const Band =
                        ((1.0 - Bb->FadeDistPercent) * T * 0.5 + Bb->FadeDistPercent) *
                        Bb->Float00;
                    double const BandSq = Band * Band;
                    if (!(DistSq > RadiusSq - BandSq))
                        Rock->Alpha = 1.0f;
                    else
                        Rock->Alpha = static_cast<float>((RadiusSq - DistSq) / BandSq);
                }
                else
                {
                    // 0x0054f27f..0x0054f29d.
                    if (!(DistSq > InnerSq))
                        Rock->Alpha = 1.0f;
                    else
                        Rock->Alpha = static_cast<float>((RadiusSq - DistSq) * InvFadeSq);
                }

                // 0x0054f23d..0x0054f255: below 0.02 (DAT_005d1fe4) nothing.
                if (Rock->Alpha < 0.02f)
                    continue;
                // 0x0054f25b..0x0054f2b3: the near fade planes (FUN_0054feb0's Float40/Float44).
                if (Depth < Bb->Float40)
                {
                    if (Depth < Bb->Float44)
                        Rock->Alpha = 0.0f;
                    else
                        Rock->Alpha = static_cast<float>(
                            (static_cast<double>(Depth) - Bb->Float44) /
                            (static_cast<double>(Bb->Float40) - Bb->Float44) * Rock->Alpha);
                }
                // 0x0054f2b5..0x0054f2d0: translucent rocks wait for the blended pass.
                if (Rock->Alpha < 1.0f)
                {
                    ++Deferred;
                    continue;
                }

                // 0x0054f2d5..0x0054f364: the corner colour. Retail reads FlipU at +0x15 of the
                // array base -- rock 0's -- and FlipV of this rock (MOVZX ECX,[ECX+0x15] with
                // ECX = the array pointer, not the element).
                int const Index =
                    static_cast<int>(Bb->Rocks[0].bFlipU) + static_cast<int>(Rock->bFlipV) * 2;
                float const* const Pick = PickCorner(Index, Corner0, Corner1, Corner2, Corner3);
                float CornerColor[3] = {Pick[0], Pick[1], Pick[2]};
                BillboardEmitRock(Bb, Rock, Right, Bb->Up, CornerColor, CenterColor);
            }

            BatchFlush(Batch);

            // 0x0054f45c..0x0054f726: the blended pass.
            if (Deferred > 0)
            {
                Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
                Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
                BillboardApplyRenderStates(Pipe, kRenderStatesBlend);
                Batch.Reset();
                int const BlendTotal = Deferred * 3;
                if (BlendTotal != 0)
                {
                    BatchBeginTriangles(Batch);
                    bool const bBlendLocked =
                        Batch.VbMgr->LockVertexMemory(Batch.Fvf,
                                                      static_cast<unsigned int>(BlendTotal),
                                                      &Batch.Lock) == 0;
                    if (bBlendLocked)
                    {
                        Batch.LockedCount = BlendTotal;
                        Batch.BindCursors((unsigned char*)Batch.Lock.Vertices,
                                          static_cast<unsigned>(Batch.Lock.Listing.Fvf));

                        int Drawn = 0;
                        for (RockIndex = 0; RockIndex < Bb->RockCount; ++RockIndex)
                        {
                            FAsteroidBillboardRock* const Rock = &Bb->Rocks[RockIndex];
                            if (Rock->bVisible == 0)
                                continue;
                            // 0x0054f562..0x0054f584: 0.02 < alpha < 1 only.
                            if (!(Rock->Alpha < 1.0f))
                                continue;
                            if (!(Rock->Alpha > 0.02f))
                                continue;
                            ++Drawn;
                            if (Drawn > Deferred)
                            {
                                FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                                      kSpaceTerrainRenderCompCpp, 0x410,
                                      "asteroid billboard render count mistake");
                                continue;
                            }
                            int const Index = static_cast<int>(Bb->Rocks[0].bFlipU) +
                                              static_cast<int>(Rock->bFlipV) * 2;
                            float const* const Pick =
                                PickCorner(Index, Corner0, Corner1, Corner2, Corner3);
                            float CornerColor[3] = {Pick[0], Pick[1], Pick[2]};
                            BillboardEmitRock(Bb, Rock, Right, Bb->Up, CornerColor, CenterColor);
                        }

                        BatchFlush(Batch);
                    }
                }
            }
        }
    }

    // 0x0054f72c..0x0054f7d0: interfaces released, the states put back.
    Batch.SetInterfaces(nullptr, nullptr);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    Pipe->SetRenderState(D3DRS_CULLMODE, 2);
    Pipe->SetRenderState(D3DRS_ALPHAREF, OldAlphaRef);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    BillboardApplyRenderStates(Pipe, kRenderStatesMaterialOff);
    BillboardApplyRenderStates(Pipe, kRenderStatesRestore);
}
