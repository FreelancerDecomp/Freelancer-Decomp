// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "win3d_engine_submit.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "common_csys.h"
#include "DALib.h"
#include "dacom.h"
#include "game_options.h"
#include "globals.h"
#include "render/IFLAppearance.h"
#include "render/IMaterial.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "x86math.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

FSpaceCamera G_Win3DSceneCamera;

// DAT_0067dd10 (win3d_engine_submit.h).
I3DMathEngine* G_Win3DObjectMath = nullptr;

namespace
{
    constexpr float kOne = 1.0f;

    constexpr D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE; // formerly kRsLighting
    constexpr D3DRENDERSTATETYPE kRsDepthFunction = D3DRS_ZFUNC; // formerly kRsCullMode

    // LAB_0059d020 (XC slot 0x00610040): FUN_0040f150 on DAT_0067dbf8, then atexit(LAB_0059d040),
    // whose body only puts the camera vtables back and tail-calls an empty base destructor --
    // nothing for the port's plain FSpaceCamera to undo.
    struct FWin3DSceneCameraStaticInit
    {
        FWin3DSceneCameraStaticInit()
        {
            SpaceCameraConstruct(&G_Win3DSceneCamera);
        }
    };
    FWin3DSceneCameraStaticInit G_Win3DSceneCameraStaticInit;

    // DAT_0067dd4c -- the 3DMathEngine FUN_005a1130 acquires on first use (FUN_005b73e0).
    I3DMathEngine* G_RayBoxMathEngine = nullptr;

    IRenderPipeline* GetPipe()
    {
        return DALib::Pipeline;
    }

    // DAT_00667e38 -- the 3DMathEngine FUN_00433f40 (0x00434176..0x004341e4) and FUN_00434310
    // (0x004344cd..0x0043453b) test and fill (FUN_005b73e0) before scaling their identity; a raw
    // scan finds no other reference, and nothing releases it.
    I3DMathEngine* G_Win3DSubmitMath = nullptr;

    // The scaled-identity block FUN_0059efa0, FUN_00433f40 and FUN_00434310 each inline: the
    // cache tested and filled (FUN_005b73e0 when null), then I3DMathEngine::Scale (+0x10) on the
    // local identity's rotation, in place (Dst and Source are the same local).
    void ScaleIdentityRotation(I3DMathEngine*& Cache, float* Mtx12, float Scale)
    {
        if (Cache == nullptr)
            Cache = Acquire3DMathEngine();
        Cache->Scale(*reinterpret_cast<FMatrix*>(Mtx12), *reinterpret_cast<FMatrix const*>(Mtx12),
                     Scale);
    }

    void ApplyCullAndLighting(IRenderPipeline* Pipe, unsigned char bNonZeroCullByte)
    {
        // FUN_0059efa0 / FUN_00434310: byte!=0 → cull=4 lighting=0; else cull=8 lighting=1.
        if (bNonZeroCullByte != 0)
        {
            Pipe->SetRenderState(kRsDepthFunction, 4);
            Pipe->SetRenderState(kRsZWriteEnable, 0);
        }
        else
        {
            Pipe->SetRenderState(kRsDepthFunction, 8);
            Pipe->SetRenderState(kRsZWriteEnable, 1);
        }
    }

    void FillIdentity3x4(float Out[12])
    {
        memset(Out, 0, 12 * sizeof(float));
        Out[0] = 1.0f;
        Out[4] = 1.0f;
        Out[8] = 1.0f;
    }

    bool SlabHitStrict(float OriginA, float OriginB, float OriginC, float DirA, float DirB,
                       float DirC, float HalfB, float HalfC)
    {
        // Listing 005a11d2..: DirA!=0, t=-(OriginA/DirA) stored as a float, t>0,
        // |OriginB+DirB*t|<HalfB (strict).
        if (DirA == 0.0f)
            return false;
        float const T = -(OriginA / DirA);
        if (!(T > 0.0f))
            return false;
        if (!(std::fabs(OriginB + DirB * T) < HalfB))
            return false;
        if (!(std::fabs(OriginC + DirC * T) < HalfC))
            return false;
        return true;
    }
}

FSpaceCamera* GetWin3DSceneCamera()
{
    // FUN_0059da60: MOV EAX, &DAT_0067dbf8. The camera is constructed at static init
    // (G_Win3DSceneCameraStaticInit below); its viewport is written only by FUN_0059dcc0
    // (Win3DSetViewportSize), from FUN_004249c0 with the created surface size.
    return &G_Win3DSceneCamera;
}

void Win3DUnprojectScreenRayDir(FSpaceCamera* Cam, float ScreenX, float ScreenY, float OutDir[3])
{
    // LAB_0055b660 @ 0055b67a..0055b760 + LAB_0040f520:
    //   Lx = (sx - ScreenCenterX) / XScale * ZNear
    //   Ly = (sy - ScreenCenterY) / YScale * ZNear
    //   Lz = -ZNear
    //   world = TransformMatrix(World[+4], L); then normalize.
    if (Cam == nullptr || OutDir == nullptr)
        return;

    float const Lx = (ScreenX - Cam->ScreenCenterX) * (Cam->ZNear / Cam->XScale);
    float const Ly = (ScreenY - Cam->ScreenCenterY) * (Cam->ZNear / Cam->YScale);
    float const Lz = -Cam->ZNear;

    float const* R = Cam->World;
    float Wx = R[0] * Lx + R[1] * Ly + R[2] * Lz;
    float Wy = R[3] * Lx + R[4] * Ly + R[5] * Lz;
    float Wz = R[6] * Lx + R[7] * Ly + R[8] * Lz;

    float const LenSq = Wx * Wx + Wy * Wy + Wz * Wz;
    if (LenSq > 0.0f)
    {
        float const InvLen = 1.0f / std::sqrt(LenSq);
        Wx *= InvLen;
        Wy *= InvLen;
        Wz *= InvLen;
    }
    OutDir[0] = Wx;
    OutDir[1] = Wy;
    OutDir[2] = Wz;
}

void SubmitWin3DEngineInstance(FClientMeshHolder* Holder, void* ArchAnchor, unsigned char bFrontCull,
                               float Scale, unsigned long Flags)
{
    // FUN_0059efa0 (thiscall on the holder, RET 0x10) -- listing 0059efa0..0059f0cd.
    // 0x0059efa0..0x0059efea: cull 4 / lighting off with bFrontCull, else cull 8 / lighting on.
    // No test of the holder, its instance, the renderer or the pipeline.
    ApplyCullAndLighting(GetPipe(), bFrontCull);

    // 0x0059eff0..0x0059f0a3: Scale != 1 (FCOMP 1.0 / TEST AH,0x44 / JNP: equal skips) scales a
    // local identity through DAT_0067dd10 and passes it with Flags | 0x30; else no matrix.
    void* Matrix = nullptr;
    unsigned long SlotFlags = Flags;
    float ScaleMtx[12];
    if (Scale != kOne)
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DObjectMath, ScaleMtx, Scale);
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }

    // 0x0059f0b4..0x0059f0c6: IRenderer2 +0x1c (ArchAnchor, holder +0x04, 1.0f, flags, matrix, 0).
#ifdef FREELANCER_WALK_MODE
    FBaseWalkInterfaceAlpha InterfaceAlpha(Holder->UserFloats[4]);
#endif
    DALib::Renderer->RenderInstance(ArchAnchor, Holder->EngineInstance, 1.0f, SlotFlags, Matrix, nullptr);
}

void SubmitWin3DTintedEngineInstance(void* ArchAnchor, void* EngineInstance,
                                     unsigned char MaterialSelector, unsigned long Flags,
                                     unsigned char const ColorRgba[4],
                                     unsigned char bCullLightingByte, float Scale)
{
    // FUN_00434310 (cdecl, 7 args) -- listing 00434310..0043460b.
    // 0x0043432a..0x00434343: nothing unless game option 0xb is on and the instance is not -1.
    if (GetGameOption(kGameOptShipWires) == 0)
        return;
    if (EngineInstance == reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1)))
        return;

    // 0x00434349..0x004343c2: cull and lighting saved (+0xf4), then set from the byte at +0x18
    // (4 / off when non-zero, else 8 / on).
    IRenderPipeline* const Pipe = GetPipe();
    int SavedCull = 0;
    int SavedLighting = 0;
    Pipe->GetRenderState(kRsDepthFunction, &SavedCull);
    Pipe->GetRenderState(kRsZWriteEnable, &SavedLighting);
    ApplyCullAndLighting(Pipe, bCullLightingByte);

    // 0x004343c8..0x0043442e: IFLAppearance from the engine; on success its Slot8 (+0x20) with
    // the +0xc byte fills the material, then the interface is released (FUN_00526a20); on
    // failure a non-null pointer is released. Neither Slot8's result nor the material is tested.
    IMaterial* Material = nullptr;
    IFLAppearance* Ifl = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
    {
        void* OutMat = nullptr;
        Ifl->GetOverrideMaterial(static_cast<unsigned long>(MaterialSelector), &OutMat);
        Material = reinterpret_cast<IMaterial*>(OutMat);
        Ifl->Release();
    }
    else if (Ifl != nullptr)
    {
        Ifl->Release();
    }

    // 0x00434431..0x004344b2: parameter 0 = the colour (type 4, each byte times the double
    // 1/255 of DAT_005c89c8, stored as floats), material +0x4c.
    alignas(4) unsigned char ValueBlob[4 + 16];
    *reinterpret_cast<unsigned long*>(ValueBlob) = 4u;
    float* const Floats = reinterpret_cast<float*>(ValueBlob + 4);
    double const ByteToUnit = 0.00392156862745098; // DAT_005c89c8
    Floats[0] = static_cast<float>(static_cast<double>(ColorRgba[0]) * ByteToUnit);
    Floats[1] = static_cast<float>(static_cast<double>(ColorRgba[1]) * ByteToUnit);
    Floats[2] = static_cast<float>(static_cast<double>(ColorRgba[2]) * ByteToUnit);
    Floats[3] = static_cast<float>(static_cast<double>(ColorRgba[3]) * ByteToUnit);
#ifdef FREELANCER_WALK_MODE
    Floats[3] *= BaseWalkGetInterfaceDrawOpacity();
#endif
    Material->SetParameterValue(0, ValueBlob);

    // 0x004344b5..0x0043456b: Scale != 1 (FCOMP 1.0 / TEST AH,0x44 / JNP: equal skips) scales a
    // local identity through DAT_00667e38 and passes it with Flags | 0x30; else no matrix.
    void* Matrix = nullptr;
    unsigned long SlotFlags = Flags;
    float ScaleMtx[12];
    if (Scale != kOne)
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DSubmitMath, ScaleMtx, Scale);
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }

    // 0x00434575..0x0043458b: IRenderer2 +0x1c (ArchAnchor, Instance, 1.0f, flags, matrix,
    // material); 0x0043458e..0x004345b6: both states restored. The value's destructor
    // (types 5..7 free, 0x004345bc) does nothing for type 4.
    DALib::Renderer->RenderInstance(ArchAnchor, EngineInstance, 1.0f, SlotFlags, Matrix, Material);
    Pipe->SetRenderState(kRsDepthFunction, SavedCull);
    Pipe->SetRenderState(kRsZWriteEnable, SavedLighting);
}

void SubmitWin3DTintedEngineInstanceWrapped(void* ArchAnchor, void* EngineInstance,
                                            unsigned char bFrontCull,
                                            unsigned char const ColorRgba[4],
                                            unsigned char bCullLightingByte, float Scale,
                                            unsigned long Flags)
{
    // FUN_00434630.
    unsigned long OrFlags = Flags | 0x8200u;
    if (bFrontCull == 0)
        OrFlags |= 0x2000u;
    // 0x00434646..0x0043466a: (Arch, Instance, 1, OrFlags, ColorRgba, bCullLightingByte,
    // Scale) -- the material selector is the constant 1, not the lighting byte.
    SubmitWin3DTintedEngineInstance(ArchAnchor, EngineInstance, 1, OrFlags, ColorRgba,
                                    bCullLightingByte, Scale);
}

int SubmitWin3DMaterialEngineInstance(void* Camera, void* EngineInstance, unsigned char bFrontCull,
                                      unsigned char const ColorRgba[4],
                                      unsigned char bCullLightingByte, float Scale,
                                      unsigned char bParam10, unsigned long Flags)
{
    // FUN_00433f40 -- listing 00433f40..00434304.
    if (EngineInstance == reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1)))
        return 1;

    // 0x00433f7b..0x00433ff9: cull and lighting saved (+0xf4), then set (+0xf0).
    IRenderPipeline* const Pipe = DALib::Pipeline;
    int SavedCull = 0;
    int SavedLighting = 0;
    Pipe->GetRenderState(kRsDepthFunction, &SavedCull);
    Pipe->GetRenderState(kRsZWriteEnable, &SavedLighting);
    ApplyCullAndLighting(Pipe, bCullLightingByte);

    // 0x00433fff..0x00434069: IFLAppearance from the engine, material 0 (Slot8 +0x20), the
    // interface released (FUN_00526a20). Retail does not test the material before using it.
    IMaterial* Material = nullptr;
    IFLAppearance* Ifl = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
    {
        void* OutMat = nullptr;
        Ifl->GetOverrideMaterial(0, &OutMat);
        Material = reinterpret_cast<IMaterial*>(OutMat);
        Ifl->Release();
    }
    else if (Ifl != nullptr)
    {
        Ifl->Release();
    }

    // 0x00434071..0x0043413d: parameter 0 = the colour (type 4, each byte times the double
    // 1/255 of DAT_005c89c8, stored as floats), parameter 0xa = bParam10 (type 1).
    alignas(4) unsigned char ColourValue[4 + 16];
    *reinterpret_cast<unsigned long*>(ColourValue) = 4u;
    float* const Colour = reinterpret_cast<float*>(ColourValue + 4);
    double const ByteToUnit = 0.00392156862745098; // DAT_005c89c8
    Colour[0] = static_cast<float>(static_cast<double>(ColorRgba[0]) * ByteToUnit);
    Colour[1] = static_cast<float>(static_cast<double>(ColorRgba[1]) * ByteToUnit);
    Colour[2] = static_cast<float>(static_cast<double>(ColorRgba[2]) * ByteToUnit);
    Colour[3] = static_cast<float>(static_cast<double>(ColorRgba[3]) * ByteToUnit);
#ifdef FREELANCER_WALK_MODE
    Colour[3] *= BaseWalkGetInterfaceDrawOpacity();
#endif
    Material->SetParameterValue(0, ColourValue);
    alignas(4) unsigned char FlagValue[4 + 16];
    memset(FlagValue, 0, sizeof(FlagValue));
    *reinterpret_cast<unsigned long*>(FlagValue) = 1u;
    *reinterpret_cast<unsigned long*>(FlagValue + 4) = (bParam10 != 0) ? 1u : 0u;
    Material->SetParameterValue(0xa, FlagValue);

    // 0x00434140..0x00434235: the flags, the optional scaled identity, IRenderer2 +0x1c.
    unsigned long SlotFlags = Flags | 0x100u;
    if (bFrontCull == 0)
        SlotFlags |= 0x2000u;
    void* Matrix = nullptr;
    float ScaleMtx[12];
    if (Scale != 1.0f) // FCOMP 1.0 (DAT_005c89b8) / TEST AH,0x44 / JNP: equal skips
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DSubmitMath, ScaleMtx, Scale); // DAT_00667e38 +0x10
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }
    int const Result =
        DALib::Renderer->RenderInstance(Camera, EngineInstance, 1.0f, SlotFlags, Matrix, Material);

    // 0x00434238..0x00434262: both states restored. The two parameter values are plain
    // (types 4 and 1), so their destructors (types 5..7 free) do nothing.
    Pipe->SetRenderState(kRsDepthFunction, SavedCull);
    Pipe->SetRenderState(kRsZWriteEnable, SavedLighting);
    return Result;
}

void SubmitWin3DOutlinedEngineInstance(void* Camera, void* EngineInstance,
                                       unsigned char bFrontCull, unsigned char const ColorRgba[4],
                                       float Scale, unsigned long Flags)
{
    // FUN_00434670 -- listing 00434670..004346e1.
    unsigned char const Black[4] = {0, 0, 0, 0};
    SubmitWin3DMaterialEngineInstance(Camera, EngineInstance, bFrontCull, Black, 0, Scale, 0,
                                      Flags);
    unsigned long OrFlags = Flags | 0x8100u;
    if (bFrontCull == 0)
        OrFlags |= 0x2000u;
    // FUN_00434310(Camera, Instance, 1, OrFlags, ColorRgba, 1, Scale).
    SubmitWin3DTintedEngineInstance(Camera, EngineInstance, 1, OrFlags, ColorRgba, 1, Scale);
}

bool Win3DRayHitsAabbHalfExtents(float const HalfExtents[3], float const Matrix3x4[12],
                                 float const RayDir[3])
{
    // FUN_005a1130 (thiscall on HalfExtents, RET 8) — listing 005a1130..005a1333.
    // All three halves 0 -> false (FCOMP 0 / TEST AH,0x44 / JP: a non-zero half goes on).
    // InverseTransform (+0x3c) of the ray direction taken as a point, through the 3DMathEngine
    // cached in DAT_0067dd4c (FUN_005b73e0 on first use):
    //   LocalDir = Out + Translation, LocalOrigin = -Translation
    // then the plane crossing on each axis whose direction is non-zero: t = -(O/D) > 0 and
    // both other coordinates strictly inside their halves (FCOMPP / TEST AH,0x41 / JNZ: a half
    // that is not above the |coordinate| fails). The Z test checks Y before X; both must pass,
    // so the order does not change the result.
    if (HalfExtents[0] == 0.0f && HalfExtents[1] == 0.0f && HalfExtents[2] == 0.0f)
        return false;

    if (G_RayBoxMathEngine == nullptr)
        G_RayBoxMathEngine = Acquire3DMathEngine();

    // The listing hands Matrix3x4 itself to the engine: nine rotation floats then the
    // translation, which is FTransform's layout.
    static_assert(sizeof(FTransform) == 12 * sizeof(float), "FTransform is the 3x4 transform");
    FVector Out;
    G_RayBoxMathEngine->InverseTransform(Out, *reinterpret_cast<FTransform const*>(Matrix3x4),
                                         *reinterpret_cast<FVector const*>(RayDir));

    float const LocalDir[3] = {Out.X + Matrix3x4[9], Out.Y + Matrix3x4[10],
                               Out.Z + Matrix3x4[11]};
    float const LocalOrigin[3] = {-Matrix3x4[9], -Matrix3x4[10], -Matrix3x4[11]};

    if (SlabHitStrict(LocalOrigin[0], LocalOrigin[1], LocalOrigin[2], LocalDir[0], LocalDir[1],
                      LocalDir[2], HalfExtents[1], HalfExtents[2]))
        return true;
    if (SlabHitStrict(LocalOrigin[1], LocalOrigin[0], LocalOrigin[2], LocalDir[1], LocalDir[0],
                      LocalDir[2], HalfExtents[0], HalfExtents[2]))
        return true;
    if (SlabHitStrict(LocalOrigin[2], LocalOrigin[0], LocalOrigin[1], LocalDir[2], LocalDir[0],
                      LocalDir[1], HalfExtents[0], HalfExtents[1]))
        return true;
    return false;
}

// Win3DButtonShapeHitRay (a port-only copy of FUN_005a0340's transform build for the flat menu
// layer's hit test) was removed 2026-10-07 with that layer; retail hit tests go through
// WinControl::HitTest (FUN_005a0340 -> FUN_005a1130).
