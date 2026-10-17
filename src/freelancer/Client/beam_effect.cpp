// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "beam_effect.h"

#include "effect_base.h"
#include "effect_type_info.h"
#include "effect_type_list.h"
#include "effect_vertex_batch.h"
#include "game_options.h"
#include "globals.h"
#include "hud_target.h"     // HudTransformPoint (FUN_004e7090)
#include "perf_options.h"
#include "texture_shapes.h"
#include "vis_effect_arch.h"

#include "DALib.h"
#include "dacom.h"
#include "common_csys.h"
#include "engine/engbase.h"
#include "space_camera.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

int G_BeamEffectLiveCount = 0;
void* G_BeamEffectTexture = nullptr;

namespace
{
    constexpr float kZero = 0.0f;
    constexpr float kHalf = 0.5f;
    constexpr float kOne = 1.0f;
    constexpr float kUvCore = 0.75f;     // 0x3f400000
    constexpr float kUvMid = 0.25f;      // 0x3e800000
    constexpr float kUvUAlt0 = 0.55f;    // 0x3f0ccccd
    constexpr float kUvUAlt1 = 0.45f;    // 0x3ee66666
    constexpr double kByteScale = 255.0; // DAT_005c89c0
    constexpr float kLifetimeIntensityCap = 0.3f; // DAT_006129cc

    bool BeamGeometryOptionEnabled()
    {
        // FUN_005792b0(0x26) == kGameOptParticles.
        return GetGameOption(kGameOptParticles) != 0;
    }

    I3DMathEngine* EffectMath()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    // The beam as the shared Effect head (same first 0x6c bytes).
    FEffectBase* AsEffect(FBeamEffect* This)
    {
        return This;
    }

    // FUN_004f7db0 used to have a twin here (BeamEffectFreezeWorld) that wrote the instance
    // TRANSFORM's translation into +0x4c instead of the object's velocity; it is gone, the
    // beam calls effect_base.cpp's EffectFreezeToWorld / EffectKill like retail (2026-09-25).

    void SetVec(FVector* Out, float X, float Y, float Z)
    {
        Out->X = X;
        Out->Y = Y;
        Out->Z = Z;
    }

    // The builders' inline colour: FLD Scale / FMUL colour (the product stays on the FPU) /
    // FMUL double 255.0 / ftol, and only AL is stored -- the low byte of the truncation, with
    // no clamp.
    unsigned char ColorByte(float Component, float Scale)
    {
        double const V = static_cast<double>(Scale) * static_cast<double>(Component) * kByteScale;
        return static_cast<unsigned char>(static_cast<long>(V));
    }

    void SetBatchColorRgb(FEffectVertexBatch* Batch, float Scale, float R, float G, float B)
    {
        // Retail ftol order: R → DAT_00674cac, G → cad, B → cae (byte slots).
        Batch->ColorR = ColorByte(R, Scale);
        Batch->ColorG = ColorByte(G, Scale);
        Batch->ColorB = ColorByte(B, Scale);
        Batch->ColorA = 0xff;
    }


}

// FUN_004f9540
FBeamEffect::FBeamEffect(unsigned TypeId) : FEffectBase(TypeId)
{
    Arch = nullptr;
    Flags70 = 0;
    std::memset(Pad71, 0, sizeof(Pad71));
    Float74 = 50.0f;
    Progress78 = -1.0f;
    Float7C = -1.0f;
    ProgressDenom80 = 0.1f;
    G_BeamEffectLiveCount = G_BeamEffectLiveCount + 1;
}

void BeamEffectConstruct(FBeamEffect* This, unsigned TypeId)
{
    ::new (This) FBeamEffect(TypeId);
}


void BeamEffectAddRef(FBeamEffect* This)
{
    // FUN_004f7a80.
    This->RefCount = This->RefCount + 1;
}

int BeamEffectRelease(FBeamEffect* This)
{
    return This->Release();
}

// FUN_004f9510 (slot +0x20) around FUN_004f9450.
FBeamEffect::~FBeamEffect()
{
    G_BeamEffectLiveCount = G_BeamEffectLiveCount - 1;
}

void BeamEffectDestroy(FBeamEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        This->~FBeamEffect();
}


void BeamEffectAttach(FBeamEffect* This, void* WatchableOwner, int EngineIndex,
                      FTransform const* LocalXform)
{
    This->Attach(WatchableOwner, EngineIndex, LocalXform);
}

// LAB_004f9600 — vtable[0] start(EffectArch*).
unsigned char FBeamEffect::Start(FEffectArch* EffectArch)
{
    FBeamEffect* const This = this;
    // 0x004f9600: arch+0x14 read with no null test on the arch.
    FBeamEffectArch* const BeamArch = EffectArch->VisBeam;
    This->Arch = BeamArch;
    if (BeamArch == nullptr)
        return 0;

    // Listing hardcodes EFT_CODE_PROJ_EFFECT (0x2c) for beam bucket.
    constexpr unsigned kBeamEffectTypeId = 0x2cu;
    This->EffectTypeId = kBeamEffectTypeId;
    FEffectTypeInfo* TypeInfo = EffectTypeInfoByIndex(static_cast<int>(kBeamEffectTypeId));
    if (TypeInfo != nullptr)
        This->Priority0C = TypeInfo->Priority;

    This->Flags08 = static_cast<unsigned char>(This->Flags08 & 0xfdu);
    // Inline link @ 004f9633..004f967f ≡ FUN_004f7af0 / EffectTypeListLink.
    EffectTypeListLink(This);

    // Already resolved once — DAT_00674f54 non-null → success (004f9682..004f9689).
    if (G_BeamEffectTexture != nullptr)
        return 1;

    // ShapeDB resolve "code_beam" — FUN_004205c0/690/6c0 on DAT_00674f24.
    // FShapeDB::Find FDUMPs the miss with the same header path (inline find at 004f969b).
    FTexShape const* Shape = G_EffectShapeDB.Find("code_beam", kShapeDbHeaderLower);
    // Listing: EDI = node+0x28 (shape payload) or 0; store in DAT_00674f54.
    G_BeamEffectTexture = Shape != nullptr ? static_cast<void*>(const_cast<FTexShape*>(Shape))
                                           : nullptr;

    if (G_BeamEffectTexture != nullptr)
        return 1;

    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x5df,
          "texture ( code_beam ) for BeamEffect not found");

    // 0x004f9735: texture miss -- link if needed, FUN_004f7db0, Flags08|2, return 0.
    EffectTypeListLink(This);
    EffectFreezeToWorld(AsEffect(This));
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    return 0;
}

unsigned char BeamEffectStart(FBeamEffect* This, FEffectArch* EffectArch)
{
    return This->FBeamEffect::Start(EffectArch);
}

// BeamEffectCreateFromEffectArch was removed 2026-09-25: it had no caller and no retail
// counterpart (operator new + unlink + delete). The real creation site is FUN_0053f340
// (cli_projectile_fx.cpp), which allocates through FUN_004f79a0(0x84).

// LAB_004f9480 — slot +8, shared with ParticleEffect: the world translation out, ST0 = +0x74.
// Retail inlines FUN_004f7ef0's body and copies the translation (+0x24 of the transform).
float FBeamEffect::GetWorldPos(FVector* Out)
{
    FBeamEffect* const This = this;
    FTransform World;
    EffectGetWorldTransform(AsEffect(This), &World);
    Out->X = World.Translation.X;
    Out->Y = World.Translation.Y;
    Out->Z = World.Translation.Z;
    return This->Float74;
}

float BeamEffectGetWorldPos(FBeamEffect* This, FVector* Out)
{
    return This->FBeamEffect::GetWorldPos(Out);
}

// LAB_004f97a0 — vtable[+0x04].
void FBeamEffect::Kill()
{
    FBeamEffect* const This = this;
    FEffectBase* const Base = AsEffect(This);

    if ((This->Flags70 & 2u) != 0u)
    {
        // 0x004f97b6: link if needed, FUN_004f7db0, dead.
        EffectTypeListLink(This);
        EffectFreezeToWorld(Base);
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return;
    }

    // 0x004f980a.
    EffectTypeListLink(This);
    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(Base) == nullptr)
        {
            // 0x004f9862: link (FUN_004fc920), FUN_004f7db0, dead; then the clamp.
            EffectTypeListLink(This);
            EffectFreezeToWorld(Base);
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        }
        else
        {
            // 0x004f9897..0x004f994f: FUN_004f7db0's body inlined; not marked dead.
            EffectFreezeToWorld(Base);
        }
    }

    // 0x004f9950: +0x7c below 0.0 (TEST AH,5 / JNP: strictly less, ordered) or above
    // DAT_006129cc (TEST AH,0x41 / JNZ skips on <=, = and unordered) becomes DAT_006129cc.
    // Exactly 0.0 and NaN are left alone.
    if (This->Float7C < 0.0f || This->Float7C > kLifetimeIntensityCap)
        This->Float7C = kLifetimeIntensityCap;
}

void BeamEffectArmDeath(FBeamEffect* This)
{
    This->FBeamEffect::Kill();
}

// LAB_004f9980 — vtable[+0x14].
unsigned char FBeamEffect::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    // 0x004f9980: no code_beam texture (DAT_00674f54) -> the visibility bit as it stands.
    if (G_BeamEffectTexture == nullptr)
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // 0x004f999b: FCOMP Priority0C vs DAT_006129c8, TEST AH,1 / JNZ -- below the perf level
    // (or unordered): the beam is killed. 0x004f9ae2..0x004f9bfe is FUN_004f7bd0 written out
    // (link; watcher gone -> CALL FUN_004f7bd0; else the freeze body; Flags08|2).
    float const Perf = GetPerfGenericEffectPerf();
    if (!(This->Priority0C >= Perf))
    {
        EffectKill(AsEffect(This));
        return static_cast<unsigned char>(This->Flags70 & 1u);
    }

    // 0x004f99b9: the object's own slot +8 (LAB_004f9480) -> world position, radius in ST0.
    FVector WorldPos;
    float const Radius = This->GetWorldPos(&WorldPos);

    // 0x004f99ca: camera slot 0x40 (the view transform at +0x38), then I3DMathEngine +0x28
    // through DAT_00674f50.
    ICamera* const CamIface = AsCamera(Cam);
    FTransform const* const ViewXform =
        reinterpret_cast<FTransform const*>(CamIface->GetViewTransform());
    FVector ViewPos;
    EffectMath()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // 0x004f99f3: NegZ = -view.z; the sphere's far edge Radius + NegZ must lie strictly beyond
    // the near plane (camera slot 0x20): FCOMP ZNear vs edge, TEST AH,5 / JP leaves otherwise.
    float const NegZ = -ViewPos.Z;
    float const FarEdge = NegZ + Radius;
    if (!(CamIface->GetZNear() < FarEdge))
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // 0x004f9a1e: the pbubble test. Distance (NegZ - Radius) must be strictly inside
    // Near + (Far - Near) * (1.0 - perf); FCOMPP + TEST AH,0x41 / JNZ leaves on <=, = or NaN.
    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    float const NearDistance = NegZ - Radius;
    float const Threshold = (TypeInfo->PbubbleFar - TypeInfo->PbubbleNear) *
                                static_cast<float>(1.0 - static_cast<double>(Perf)) +
                            TypeInfo->PbubbleNear;
    if (!(Threshold > NearDistance))
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // 0x004f9a56 / 0x004f9a79: the two side planes (camera +0x80/+0x88 and +0x90/+0x94).
    // TEST AH,0x41 / JP -> outside when the plane distance is greater than the radius or NaN.
    float const DistX = fabsf(ViewPos.X) * Cam->PlaneX[0] + ViewPos.Z * Cam->PlaneX[2];
    float const DistY = fabsf(ViewPos.Y) * Cam->PlaneY[1] + ViewPos.Z * Cam->PlaneY[2];
    if (!(DistX <= Radius) || !(DistY <= Radius))
    {
        This->Flags70 = static_cast<unsigned char>(This->Flags70 & 0xfeu);   // 0x004f9ac0
        return static_cast<unsigned char>(This->Flags70 & 1u);
    }

    This->Flags70 = static_cast<unsigned char>(This->Flags70 | 1u);           // 0x004f9a9c
    return static_cast<unsigned char>(This->Flags70 & 1u);
}

unsigned char BeamEffectUpdateVisibility(FBeamEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FBeamEffect::UpdateVisibility(Dt, Cam);
}

// LAB_004f9c20 — vtable[+0x18].
unsigned char FBeamEffect::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    // 0x004f9c25: FUN_005792b0(0x26).
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    // 0x004f9c3f: FUN_0042d740.
    if (ShouldSkipLocalServerTick())
        return 1;

    // 0x004f9c4c: TEST AH,1 / JNZ -- below zero (or NaN) restarts at 0, else accumulates.
    if (!(This->Progress78 >= 0.0f))
        This->Progress78 = 0.0f;
    else
        This->Progress78 = This->Progress78 + Dt;

    // 0x004f9c6d: TEST AH,0x41 / JNZ -- no lifetime (<= 0 or NaN): keep drawing.
    if (!(This->Float7C > 0.0f))
        return 1;

    // 0x004f9c81: TEST AH,0x41 / JP -- still above zero (or NaN) after the tick: keep drawing.
    This->Float7C = This->Float7C - Dt;
    if (!(This->Float7C <= 0.0f))
        return 1;

    // 0x004f9c9c..0x004f9dc5: FUN_004f7bd0 written out, then the explicit `OR [+8],2` retail
    // repeats after the recursive-call arm (already set by the kill).
    EffectKill(AsEffect(This));
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    return 0;
}

unsigned char BeamEffectTickDraw(FBeamEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FBeamEffect::TickDraw(Dt, Cam);
}

// LAB_004fa0a0 — listing txt:389985–390134 (MulTransform path included).
unsigned char FBeamEffect::BuildGeometry(FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    This->Flags70 = static_cast<unsigned char>(This->Flags70 & 0xfeu);
    if (!BeamGeometryOptionEnabled())
        return 0;

    FTransform StackXform;
    FTransform* UseXform = &This->LocalXform;

    // 0x004fa0c5..0x004fa177: with a watched object (the Watchable at +0x44, and that minus
    // eight, both non-null) and an engine instance (+0x40 != -1), the instance transform
    // (IEngine slot 0xa0) times the local one (math engine slot 0x1c, DAT_00674f50);
    // otherwise the local transform at +0x10. Retail copies either into a stack local.
    Watchable const* const Watched = This->Watcher.target();
    if (Watched != nullptr && reinterpret_cast<uintptr_t>(Watched) - 8u != 0u &&
        This->EngineIndex != -1)
    {
        float* const InstanceRaw = DALib::Engine->GetInstanceTransform(
            reinterpret_cast<void*>(static_cast<intptr_t>(This->EngineIndex)));
        EffectMath()->MulTransform(StackXform, *reinterpret_cast<FTransform const*>(InstanceRaw),
                                   This->LocalXform);
        UseXform = &StackXform;
    }

    // 0x004fa17c..0x004fa1aa: T = +0x78 / +0x80 only when +0x78 is below +0x80 (TEST AH,5 /
    // JP skips equal, above and unordered), else 1.
    float T = 1.0f;
    if (This->Progress78 < This->ProgressDenom80)
        T = This->Progress78 / This->ProgressDenom80;

    // 0x004fa1ae..0x004fa1e6: with +0x7c above 0, intensity = +0x7c / 0.3f (DAT_006129cc); the
    // quotient still on the FPU at or below 0 returns 0 (TEST AH,0x41 / JP).
    float Intensity = 1.0f;
    if (This->Float7C > 0.0f)
    {
        double const Quotient = static_cast<double>(This->Float7C) / 0.3f;
        Intensity = static_cast<float>(Quotient);
        if (Quotient <= 0.0)
            return 0;
    }

    FBeamEffectArch* const Arch = This->Arch;

    if ((This->Flags70 & 2u) != 0u)
    {
        BeamEffectBuildSingleSegment(This, UseXform, 0.0f, Intensity);
        return 1;
    }
    if (T == 0.0f)
    {
        BeamEffectBuildSingleSegment(This, UseXform, 0.0f, Intensity);
        return 1;
    }

    if (Arch->SegmentMode == 0)
        BeamEffectBuildSingleSegment(This, UseXform, T, Intensity);
    else if (Arch->SegmentMode == 1)
        BeamEffectBuildMultiSegment(This, UseXform, T, Intensity);
    return 1;
}

unsigned char BeamEffectBuildGeometry(FBeamEffect* This, FSpaceCamera* Cam)
{
    return This->FBeamEffect::BuildGeometry(Cam);
}

// FUN_004fa270 (thiscall, RET 0xc, returns AL = 1) — freelancer.exe.txt:390144–391149.
int BeamEffectBuildSingleSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                 float Intensity)
{
    FBeamEffectArch* const Arch = This->Arch;
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    // --- radius setup (0x004fa270–0x004fa2b3) ---
    float NearR = Arch->RadiusNear;
    if (T == 0.0f && (This->Flags70 & 8u) != 0u)
    {
        // 0x004fa2a0: FUN_0054bc80 (IsFirstPersonCameraCurrent, space_camera.cpp).
        if (!IsFirstPersonCameraCurrent())
            NearR = Arch->RadiusNearAlt;
    }

    float TipR = Arch->RadiusTip;
    if (TipR < NearR)
        TipR = NearR;

    // 0x004fa2ca..0x004fa394: no clamps here (unlike FUN_004fb1e0). The Z sums keep the
    // product on the FPU and round once at the store.
    float const Delta0 = Arch->Radius0 - NearR;
    float const Delta1 = Arch->Radius1 - NearR;
    float const EndR = static_cast<float>(static_cast<double>(Delta1) * T + NearR);
    float const StartRNeg = static_cast<float>(-(static_cast<double>(Delta0) * T + NearR));

    FVector WorldT;
    WorldT.X = WorldXform->Translation.X;
    WorldT.Y = WorldXform->Translation.Y;
    WorldT.Z = WorldXform->Translation.Z;

    // Local axis samples (FUN_00422b80 = Vector3 store; FUN_004e7090 = TransformXform).
    // Cross arms live in local XY; beam extent is local Z.
    // Listing: EndR at +0xa4 with zeros at +0x9c/+0xa0 → src (0,0,EndR);
    // StartRNeg at +0xb0 with zeros at +0xa8/+0xac → src (0,0,StartRNeg).
    FVector VX;
    SetVec(&VX, NearR, 0.0f, 0.0f);
    FVector VY;
    SetVec(&VY, 0.0f, NearR, 0.0f);
    FVector TipX;
    SetVec(&TipX, TipR, 0.0f, 0.0f);
    FVector TipY;
    SetVec(&TipY, 0.0f, TipR, 0.0f);
    FVector EndZ;
    SetVec(&EndZ, 0.0f, 0.0f, EndR);
    FVector StartZ;
    SetVec(&StartZ, 0.0f, 0.0f, StartRNeg);

    FVector XfPosX;
    FVector XfNegX;
    FVector XfPosY;
    FVector XfNegY;
    FVector XfTipPosX;
    FVector XfTipNegX;
    FVector XfTipPosY;
    FVector XfTipNegY;
    FVector XfEnd;   // stack +0x30 after transforms
    FVector XfStart; // stack +0x90
    FVector Tmp;

    // 0x004fa3c0..0x004fa44e: four FUN_004e7090 calls (HudTransformPoint) -- the NearR axes
    // and their negations (FUN_00422b80 on negated components).
    HudTransformPoint(WorldXform, &XfPosX, &VX);
    SetVec(&Tmp, -VX.X, -VX.Y, -VX.Z);
    HudTransformPoint(WorldXform, &XfNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfPosY, &VY);
    SetVec(&Tmp, -VY.X, -VY.Y, -VY.Z);
    HudTransformPoint(WorldXform, &XfNegY, &Tmp);

    // 0x004fa453..0x004fa592: six inline TransformXform calls through DAT_00674f50 (re-read and
    // lazily acquired before each), straight into their destinations: the tip axes, their
    // negations, then Start (+0x90) and End (+0x30).
    EffectMath()->TransformXform(XfTipPosX, *WorldXform, TipX);
    SetVec(&Tmp, -TipX.X, -TipX.Y, -TipX.Z);
    EffectMath()->TransformXform(XfTipNegX, *WorldXform, Tmp);
    EffectMath()->TransformXform(XfTipPosY, *WorldXform, TipY);
    SetVec(&Tmp, -TipY.X, -TipY.Y, -TipY.Z);
    EffectMath()->TransformXform(XfTipNegY, *WorldXform, Tmp);
    EffectMath()->TransformXform(XfStart, *WorldXform, StartZ);
    EffectMath()->TransformXform(XfEnd, *WorldXform, EndZ);

    // --- UV endpoints from Arch+0x8 / +0xc (0x004fa595–0x004fa5f6) ---
    float UvV0;
    float UvV1;
    if (Arch->UvModeV == 1)
    {
        UvV0 = 0.0f;
        UvV1 = kHalf;
    }
    else
    {
        UvV0 = kHalf;
        UvV1 = kOne;
    }
    float const UvVMid = (UvV0 + UvV1) * static_cast<float>(kHalf); // *0.5 double

    float UvU0;
    float UvU1;
    if (Arch->UvModeU == 1)
    {
        UvU0 = 0.0f;
        UvU1 = kUvUAlt1; // 0.45
    }
    else
    {
        UvU0 = kUvUAlt0; // 0.55
        UvU1 = kOne;
    }

    float const Scale0 = Intensity * Arch->IntensityScale0;
    float const Scale1 = Intensity * Arch->IntensityScale1;

    // --- tip cross, listing-verified (0x004fa612–0x004fadfd + first fan) ---
    // V0 WorldT (+0x40); V1 tip+X (+0xf0); V2 tip+Y (+0x84); V3 tip−X (+0xd8);
    // V4 tip−Y (+0xc0). Fan: (0,1,2)(0,3,2)(0,3,4)(0,1,4).
    SetBatchColorRgb(Batch, Scale0, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = kUvCore;
    Batch->V = UvVMid;
    unsigned short const I0 =
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale0, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kHalf;
    Batch->V = UvV0;
    unsigned short const I1 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipPosX.X, XfTipPosX.Y, XfTipPosX.Z));

    Batch->V = UvV1;
    unsigned short const I2 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipPosY.X, XfTipPosY.Y, XfTipPosY.Z));

    Batch->U = kOne;
    Batch->V = UvV1;
    unsigned short const I3 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipNegX.X, XfTipNegX.Y, XfTipNegX.Z));

    Batch->V = UvV0;
    unsigned short const I4 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipNegY.X, XfTipNegY.Y, XfTipNegY.Z));

    // --- second structure 0x004fae02–0x004fb1cb (NOT a tip-radius mirror) ---
    // Radii: NearR XY arms + local-Z Start/End (Arch+0x60/+0x64 deltas). Scale1.
    // U-channel UVs from Arch+0xc. Tip fan indices are pushed AFTER these verts
    // (listing 0x004fb099 — one fcb40 for the whole function).
    // A0 WorldT outer U=0 V=UvU0
    // A1 XfStart core  U=0.25 V=UvU1
    // A2 XfEnd   mid   U=0.25 V=UvU1
    // A3 XfPosX  tip   U=0.25 V=UvU0
    // A4 XfNegX / A5 XfPosY / A6 XfNegY (reuse tip colour/UV)
    SetBatchColorRgb(Batch, Scale1, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = 0.0f;
    Batch->V = UvU0;
    unsigned short const A0 =
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorCoreR, Arch->ColorCoreG, Arch->ColorCoreB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A1 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfStart.X, XfStart.Y, XfStart.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorMidR, Arch->ColorMidG, Arch->ColorMidB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A2 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfEnd.X, XfEnd.Y, XfEnd.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kUvMid;
    Batch->V = UvU0;
    unsigned short const A3 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfPosX.X, XfPosX.Y, XfPosX.Z));

    unsigned short const A4 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfNegX.X, XfNegX.Y, XfNegX.Z));
    unsigned short const A5 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfPosY.X, XfPosY.Y, XfPosY.Z));
    unsigned short const A6 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfNegY.X, XfNegY.Y, XfNegY.Z));

    // All indices (tip + second) — listing 0x004fb099–0x004fb1cb.
    EffectVertexBatchEnsureIndices(Batch, 3);
    Batch->Indices[Batch->IndexCount] = I0;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I1;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I2;
    Batch->IndexCount = Batch->IndexCount + 1;
    EffectVertexBatchPushTriangle(Batch, I0, I3, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I4);
    EffectVertexBatchPushTriangle(Batch, I0, I1, I4);

    EffectVertexBatchPushTriangle(Batch, A0, A1, A3);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A4);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A5);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A6);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A3);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A4);
    // 0x004fb1ae reads frame +0x3c, which held A0 until 0x004fb16d stored the MOVZX'd A5 over
    // it (MOV [ESP+0x48],EAX with three arguments pushed); A0 itself stays in ESI.
    EffectVertexBatchPushTriangle(Batch, A0, A2, A5);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A6);
    return 1;
}

// FUN_00423b30 (cdecl): Out = A + B, per component.
static void VecAdd(FVector* Out, FVector const* A, FVector const* B)
{
    Out->X = A->X + B->X;
    Out->Y = A->Y + B->Y;
    Out->Z = A->Z + B->Z;
}

// FUN_00411350 (thiscall on In, RET 4) and the FUN_00422b80 ctor on negated components: Out = -In.
static void VecNegate(FVector* Out, FVector const* In)
{
    Out->X = -In->X;
    Out->Y = -In->Y;
    Out->Z = -In->Z;
}

// FUN_004fb1e0 (thiscall, RET 0xc, returns AL = 1) -- the SegmentMode 1 ("BeamBolt") builder,
// txt:391152-392620. A tip cross (TipR) fanned from the origin, then a tube: origin -> Start
// behind it, the NearR ring around the origin, the ring pushed forward to Mid, and Mid ->
// Mid+End in front. Frame locals are named in comments by their offset from ESP after the
// 0x16c SUB and the four saves.
int BeamEffectBuildMultiSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                float Intensity)
{
    FBeamEffectArch* const Arch = This->Arch;
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    // 0x004fb1e9..0x004fb219: NearR = +0x6c, or flash_size +0x78 when T == 0 (TEST AH,0x44 /
    // JP keeps +0x6c on unequal and unordered); TipR = +0x74, raised to NearR when below it
    // (FCOM, TEST AH,5 / JP).
    float NearR = Arch->RadiusNear;
    if (T == 0.0f)
        NearR = Arch->RadiusNearAlt;
    float TipR = Arch->RadiusTip;
    if (TipR < NearR)
        TipR = NearR;

    // 0x004fb21b..0x004fb273: the two lengths less NearR and core_length, each zeroed when
    // below 0 (TEST AH,5 / JP keeps equal, above and unordered).
    float Delta0 = Arch->Radius0 - NearR;   // +0x28
    float Delta1 = Arch->Radius1 - NearR;   // +0x20
    float MidR = Arch->RadiusMid;           // +0x14
    if (Delta0 < 0.0f)
        Delta0 = 0.0f;
    if (Delta1 < 0.0f)
        Delta1 = 0.0f;
    if (MidR < 0.0f)
        MidR = 0.0f;

    // 0x004fb27b..0x004fb377: the local vectors. The Z sums keep the product on the FPU and
    // round once at the store.
    FVector NearX;                                         // +0x60
    SetVec(&NearX, NearR, 0.0f, 0.0f);
    FVector NearY;                                         // +0x3c
    SetVec(&NearY, 0.0f, NearR, 0.0f);
    FVector TipX;                                          // +0x6c
    SetVec(&TipX, TipR, 0.0f, 0.0f);
    FVector TipY;                                          // +0x78
    SetVec(&TipY, 0.0f, TipR, 0.0f);
    FVector WorldT;                                        // +0x48
    WorldT.X = WorldXform->Translation.X;
    WorldT.Y = WorldXform->Translation.Y;
    WorldT.Z = WorldXform->Translation.Z;
    FVector MidLocal;                                      // +0x54
    SetVec(&MidLocal, 0.0f, 0.0f, MidR * T);
    FVector StartLocal;                                    // +0x14c
    SetVec(&StartLocal, 0.0f, 0.0f,
           static_cast<float>(-(static_cast<double>(Delta0) * T + NearR)));
    FVector EndLocal;                                      // +0x128
    SetVec(&EndLocal, 0.0f, 0.0f, static_cast<float>(static_cast<double>(Delta1) * T + NearR));

    // 0x004fb379..0x004fb4ae: nine FUN_004e7090 calls (HudTransformPoint) -- each axis, then
    // its negation (FUN_00411350 / FUN_00422b80 into +0x30), then the Start point.
    FVector Tmp;                                           // +0x30
    FVector XfNearPosX;                                    // +0x134
    FVector XfNearNegX;                                    // +0x104
    FVector XfNearPosY;                                    // +0x158
    FVector XfNearNegY;                                    // +0xbc
    FVector XfTipPosX;                                     // +0x140
    FVector XfTipNegX;                                     // +0xd4
    FVector XfTipPosY;                                     // +0x11c
    FVector XfTipNegY;                                     // +0xb0
    FVector XfStart;                                       // +0xec
    HudTransformPoint(WorldXform, &XfNearPosX, &NearX);
    VecNegate(&Tmp, &NearX);
    HudTransformPoint(WorldXform, &XfNearNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfNearPosY, &NearY);
    VecNegate(&Tmp, &NearY);
    HudTransformPoint(WorldXform, &XfNearNegY, &Tmp);
    HudTransformPoint(WorldXform, &XfTipPosX, &TipX);
    VecNegate(&Tmp, &TipX);
    HudTransformPoint(WorldXform, &XfTipNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfTipPosY, &TipY);
    VecNegate(&Tmp, &TipY);
    HudTransformPoint(WorldXform, &XfTipNegY, &Tmp);
    HudTransformPoint(WorldXform, &XfStart, &StartLocal);

    // 0x004fb4b3..0x004fb671: six inline TransformXform calls through DAT_00674f50 (re-read and
    // lazily acquired before each), straight into their destinations; the sums are
    // FUN_00423b30.
    FVector Sum;
    FVector XfEndMid;                                      // +0xe0
    FVector XfMid;                                         // +0xc8
    FVector XfNearPosXMid;                                 // +0xf8
    FVector XfNearNegXMid;                                 // +0x110
    FVector XfNearPosYMid;                                 // +0xa4
    FVector XfNearNegYMid;                                 // +0x30
    VecAdd(&Sum, &EndLocal, &MidLocal);
    EffectMath()->TransformXform(XfEndMid, *WorldXform, Sum);
    EffectMath()->TransformXform(XfMid, *WorldXform, MidLocal);
    VecAdd(&Sum, &NearX, &MidLocal);
    EffectMath()->TransformXform(XfNearPosXMid, *WorldXform, Sum);
    VecNegate(&Tmp, &NearX);
    VecAdd(&Sum, &Tmp, &MidLocal);
    EffectMath()->TransformXform(XfNearNegXMid, *WorldXform, Sum);
    VecAdd(&Sum, &NearY, &MidLocal);
    EffectMath()->TransformXform(XfNearPosYMid, *WorldXform, Sum);
    VecNegate(&Tmp, &NearY);
    VecAdd(&Sum, &Tmp, &MidLocal);
    EffectMath()->TransformXform(XfNearNegYMid, *WorldXform, Sum);

    // 0x004fb674..0x004fb6d5: head_texture (+0x08) == 1 -> V {0, 0.5}, else {0.5, 1}, and the
    // midpoint (sum * 0.5 double, kept on the FPU until the first vertex stores it);
    // trail_texture (+0x0c) == 1 -> {0, 0.45}, else {0.55, 1}.
    float UvV0;                                            // +0x24
    float UvV1;                                            // +0x14
    if (Arch->UvModeV == 1)
    {
        UvV0 = 0.0f;
        UvV1 = kHalf;
    }
    else
    {
        UvV0 = kHalf;
        UvV1 = kOne;
    }
    float const UvVMid = static_cast<float>((static_cast<double>(UvV0) + UvV1) * 0.5);

    float UvU0;                                            // +0x20
    float UvU1;                                            // +0x28
    if (Arch->UvModeU == 1)
    {
        UvU0 = 0.0f;
        UvU1 = kUvUAlt1;
    }
    else
    {
        UvU0 = kUvUAlt0;
        UvU1 = kOne;
    }

    // 0x004fb6d5..0x004fb6ed: head_brightness and trail_brightness times the intensity.
    float const Scale0 = Intensity * Arch->IntensityScale0;   // +0x2c
    float const Scale1 = Intensity * Arch->IntensityScale1;   // +0x10

    // --- tip cross, head_brightness (0x004fb6f1..0x004fbf40) ---
    // Colour bytes are ftol(Scale * colour * 255.0) (the low byte of the truncation), alpha
    // 0xff. The first eight appends are FUN_0046eab0 written out inline.
    SetBatchColorRgb(Batch, Scale0, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = kUvCore;
    Batch->V = UvVMid;
    unsigned short const I0 =                              // +0x90
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale0, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kHalf;
    Batch->V = UvV0;
    unsigned short const I1 = static_cast<unsigned short>( // +0x98
        EffectVertexBatchAppend(Batch, XfTipPosX.X, XfTipPosX.Y, XfTipPosX.Z));
    // 0x004fba8a: V is read from +0x24 before that slot is reused for the position -- still V0.
    Batch->U = kOne;
    Batch->V = UvV0;
    unsigned short const I2 = static_cast<unsigned short>( // +0x2c
        EffectVertexBatchAppend(Batch, XfTipPosY.X, XfTipPosY.Y, XfTipPosY.Z));
    Batch->U = kOne;
    Batch->V = UvV1;
    unsigned short const I3 = static_cast<unsigned short>( // EBP
        EffectVertexBatchAppend(Batch, XfTipNegX.X, XfTipNegX.Y, XfTipNegX.Z));
    Batch->U = kHalf;
    Batch->V = UvV1;
    unsigned short const I4 = static_cast<unsigned short>( // +0x84
        EffectVertexBatchAppend(Batch, XfTipNegY.X, XfTipNegY.Y, XfTipNegY.Z));

    // --- tube, trail_brightness (0x004fbf45..0x004fc738) ---
    SetBatchColorRgb(Batch, Scale1, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = 0.0f;
    Batch->V = UvU1;
    unsigned short const A0 =                              // +0x8c
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorCoreR, Arch->ColorCoreG, Arch->ColorCoreB);
    Batch->U = kUvMid;
    Batch->V = UvU0;
    unsigned short const A1 = static_cast<unsigned short>( // +0x88
        EffectVertexBatchAppend(Batch, XfStart.X, XfStart.Y, XfStart.Z));

    // 0x004fc2f0..0x004fc4d6: outter_color, U 0.25, V = the second trail V; the NearR ring.
    // From here on the appends are real FUN_0046eab0 calls.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A2 = static_cast<unsigned short>( // +0x1c
        EffectVertexBatchAppend(Batch, XfNearPosX.X, XfNearPosX.Y, XfNearPosX.Z));
    unsigned short const B0 = static_cast<unsigned short>( // +0x18
        EffectVertexBatchAppend(Batch, XfNearNegX.X, XfNearNegX.Y, XfNearNegX.Z));
    unsigned short const B1 = static_cast<unsigned short>( // +0x14
        EffectVertexBatchAppend(Batch, XfNearPosY.X, XfNearPosY.Y, XfNearPosY.Z));
    unsigned short const B2 = static_cast<unsigned short>( // +0x24
        EffectVertexBatchAppend(Batch, XfNearNegY.X, XfNearNegY.Y, XfNearNegY.Z));

    // 0x004fc54f..0x004fc5d3: sec_core_color, U 0, the first trail V; Mid.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorBand1R, Arch->ColorBand1G, Arch->ColorBand1B);
    Batch->U = 0.0f;
    Batch->V = UvU0;
    unsigned short const B3 = static_cast<unsigned short>( // +0xa0
        EffectVertexBatchAppend(Batch, XfMid.X, XfMid.Y, XfMid.Z));

    // 0x004fc5d8..0x004fc65f: tail_color, U 0.25, the second trail V; Mid + End.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorMidR, Arch->ColorMidG, Arch->ColorMidB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const B4 = static_cast<unsigned short>( // +0x94 (stored after SUB ESP,0xc)
        EffectVertexBatchAppend(Batch, XfEndMid.X, XfEndMid.Y, XfEndMid.Z));

    // 0x004fc664..0x004fc738: FUN_004fc950 with the float-rounded products trail_brightness *
    // sec_outter_color, U 0.25, the first trail V; the ring pushed forward to Mid.
    EffectVertexBatchSetColor(Batch, Scale1 * Arch->ColorBand2R, Scale1 * Arch->ColorBand2G,
                              Scale1 * Arch->ColorBand2B);
    Batch->V = UvU0;
    Batch->U = kUvMid;
    unsigned short const C0 = static_cast<unsigned short>( // +0x20
        EffectVertexBatchAppend(Batch, XfNearPosXMid.X, XfNearPosXMid.Y, XfNearPosXMid.Z));
    unsigned short const C1 = static_cast<unsigned short>( // +0x10
        EffectVertexBatchAppend(Batch, XfNearNegXMid.X, XfNearNegXMid.Y, XfNearNegXMid.Z));
    unsigned short const C2 = static_cast<unsigned short>( // +0x9c
        EffectVertexBatchAppend(Batch, XfNearPosYMid.X, XfNearPosYMid.Y, XfNearPosYMid.Z));
    unsigned short const C3 = static_cast<unsigned short>( // +0x28 (stored after three PUSHes)
        EffectVertexBatchAppend(Batch, XfNearNegYMid.X, XfNearNegYMid.Y, XfNearNegYMid.Z));

    // 0x004fc73d..0x004fc88f: FUN_004fc9a0 triangles and FUN_004fca50 quads.
    EffectVertexBatchPushTriangle(Batch, I0, I1, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I4);
    EffectVertexBatchPushTriangle(Batch, I0, I1, I4);

    EffectVertexBatchPushTriangle(Batch, A0, A1, A2);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B0);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B1);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B2);

    EffectVertexBatchPushQuad(Batch, A0, B3, C0, A2);
    EffectVertexBatchPushQuad(Batch, A0, B3, C1, B0);
    EffectVertexBatchPushQuad(Batch, A0, B3, C2, B1);
    EffectVertexBatchPushQuad(Batch, A0, B3, C3, B2);

    EffectVertexBatchPushTriangle(Batch, B3, B4, C0);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C1);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C2);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C3);
    return 1;
}
