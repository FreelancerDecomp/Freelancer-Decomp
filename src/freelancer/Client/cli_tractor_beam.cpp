// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cli_tractor_beam.h"

#include "client_equip_fx.h"   // FClientEquip
#include "effect_type_list.h"  // EffectTypeListLink (FUN_004f7af0)
#include "space_camera.h"      // FSpaceCamera
#include "texture_shapes.h"    // G_EffectShapeDB (DAT_00674f24)
#include "win3d_immediate.h"   // Win3DDrawFuzzyLine (FUN_00423be0), state setters
#include "win3d_render.h"      // Win3DApplyCamera (FUN_00422950)

#include "common_cequip.h"     // CETractor, CEquip::IsFunctioning
#include "common_archetype.h"  // Archetype::Tractor::MaxLength (+0x70)
#include "common_cobject.h"    // CLoot
#include "common_csys.h"       // LookMatrixYup, Acquire3DMathEngine (FUN_005b73e0)
#include "x86math.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace
{
    // DAT_005c7f68 (0x3b808081): 1/255.
    constexpr float kByteToUnit = 1.0f / 255.0f;

    // The tractor effect's type bucket, EFT_TRACTORBEAM (0x0052fa42 PUSH 0x2d).
    constexpr unsigned kTractorBeamEffectType = 0x2du;

    // LAB_00512da0's constants: the line jitter 0.2f (0x3e4ccccd), the beam width 5.0f
    // (0x40a00000), DAT_005cf100 (0.2 as a double, the loot radius scale), the end jitter 0.5f
    // (0x3f000000), DAT_005db708 (0.8 pi as a double), DAT_005d3e08 (0.3f) and 2 pi (0x40c90fdb).
    constexpr float kLineJitterScale = 0.2f;
    constexpr float kBeamWidth = 5.0f;
    constexpr double kLootRadiusScale = 0.2;
    constexpr float kEndJitter = 0.5f;
    constexpr double kSparkSpreadScale = 2.5132741928100586;
    constexpr float kSparkPullBack = 0.3f;
    constexpr float kTwoPi = 6.2831855f;

    // DAT_005da778: 1/32767 as a float.
    constexpr float kRandUnit = 1.0f / 32767.0f;

    // DAT_006751b0 -- the beam draw's 3DMathEngine, FUN_005b73e0 on first use (0x00513259).
    I3DMathEngine* G_TractorBeamMath = nullptr;

    // FUN_0053ebe0 (cdecl Scale): `rand() * Scale * DAT_005da778`, left on the FPU. Its only three
    // callers are in LAB_00512da0.
    double RandomScaled(float Scale)
    {
        return static_cast<double>(rand()) * Scale * kRandUnit;
    }

    // [[this+0x6c]+4]: the wrapper's equipment, used as the CETractor without a cast.
    CETractor* BeamTractor(FCliTractorBeam const* Self)
    {
        return static_cast<CETractor*>(Self->Owner->Equip);
    }


}

float FCliTractorBeam::GetWorldPos(FVector* Out)
{
    FCliTractorBeam* const Self = this;
    // LAB_005133b0 (RET 4): CETractor::GetSourcePos into a local, copied into *Out; then
    // CETractor::TractorArch()->+0x70 (max_length) is left on the FPU as the radius.
    CETractor* const Tractor = BeamTractor(Self);
    Vector const Source = Tractor->GetSourcePos();
    Out->X = Source.x;
    Out->Y = Source.y;
    Out->Z = Source.z;
    return Tractor->TractorArch()->MaxLength;
}

unsigned char FCliTractorBeam::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FCliTractorBeam* const Self = this;
    // LAB_005133f0 (RET 8): FUN_004f7b50 with the camera (the second argument) only.
    return EffectInCameraView(Self, Cam);
}

unsigned char FCliTractorBeam::BuildGeometry(FSpaceCamera* Cam)
{
    FCliTractorBeam* const Self = this;
    // LAB_00512da0 (RET 4). 0x00512da9..0x00512dca: the tractor's vtable +0xc
    // (IsFunctioning); false returns it (AL = 0) before anything is drawn.
    CETractor* const Tractor = BeamTractor(Self);
    if (!Tractor->IsFunctioning())
        return 0;

    // 0x00512dcf..0x00512e4a: the source (CETractor::GetSourcePos), then the line record's
    // fixed fields: the colour (+0x70), the "line" shape's texture (its first dword; no null
    // test), jitter 0.2f (0x3e4ccccd), the eye (ICamera +0x0c) and side scale 1.0f. Retail
    // leaves the rest of its stack record unwritten until each draw fills it; the port
    // value-initialises it (no field is read before it is written).
    Vector const Source = Tractor->GetSourcePos();
    FWin3DFuzzyLine Line = {};
    Line.Color[0] = Self->Color[0];
    Line.Color[1] = Self->Color[1];
    Line.Color[2] = Self->Color[2];
    Line.Texture = Self->LineShape->Texture;
    Line.JitterScale = kLineJitterScale;
    float const* const Eye = AsCamera(Cam)->GetPosition();
    Line.EyePos[0] = Eye[0];
    Line.EyePos[1] = Eye[1];
    Line.EyePos[2] = Eye[2];
    Line.SideScale = 1.0f;

    // 0x00512e4a..0x00512e5a: depth write off, culling off, the camera.
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DCullFace);
    Win3DApplyCamera(AsCamera(Cam));

    // 0x00512e5f..0x0051336f: the arms, [+0x54, +0x58) stride 0x24. The drawn-arm count is
    // tested before every arm (JG 0xa), so at most eleven are drawn.
    std::vector<TractorArm> const& Arms = Tractor->GetTractorArms();
    int Drawn = 0;
    for (size_t ArmIndex = 0; ArmIndex < Arms.size(); ++ArmIndex)
    {
        if (Drawn > 10)
            break;
        TractorArm const& Arm = Arms[ArmIndex];

        // 0x00512e8b..0x00512e9e: the target watcher's Watchable and the CLoot 0x9c before
        // it (TractorArm::GetTarget inlined); either null skips the arm without counting it.
        if (Arm.TargetWatch.target() == nullptr)
            continue;
        CLoot* const Loot = Arm.GetTarget();
        if (Loot == nullptr)
            continue;

        // 0x00512ea4..0x00512f72: the tip (TractorArm::GetTipPos), the source -> tip distance
        // (y*y + z*z) + x*x stored as a float, and the unit direction (times 1.0f / distance).
        Vector const Tip = Arm.GetTipPos();
        float const ToTip[3] = {Tip.x - Source.x, Tip.y - Source.y, Tip.z - Source.z};
        float const TipDistance = static_cast<float>(
            sqrt(static_cast<double>(ToTip[1]) * ToTip[1] +
                 static_cast<double>(ToTip[2]) * ToTip[2] +
                 static_cast<double>(ToTip[0]) * ToTip[0]));
        double const InvTipDistance = 1.0 / TipDistance;
        float const TipDir[3] = {static_cast<float>(ToTip[0] * InvTipDistance),
                                 static_cast<float>(ToTip[1] * InvTipDistance),
                                 static_cast<float>(ToTip[2] * InvTipDistance)};

        // 0x00512f16..0x00512fc6: the loot's position (+0x2c) and the source -> loot vector.
        Vector const LootPos = Loot->WorldPosition;
        float const ToLoot[3] = {LootPos.x - Source.x, LootPos.y - Source.y,
                                 LootPos.z - Source.z};
        float const LootDistance = static_cast<float>(
            sqrt(static_cast<double>(ToLoot[1]) * ToLoot[1] +
                 static_cast<double>(ToLoot[2]) * ToLoot[2] +
                 static_cast<double>(ToLoot[0]) * ToLoot[0]));

        // 0x00512fca..0x0051300c: the loot's physical radius (CObject +0x64) times 0.2
        // (DAT_005cf100, a double); Reach = distance to its shell, Excess = how far the tip
        // is beyond it.
        Vector RadiusCentre;
        float const Radius =
            static_cast<float>(Loot->get_physical_radius_r(RadiusCentre) * kLootRadiusScale);
        float const Reach = LootDistance - Radius;
        float EndPoint[3] = {Tip.x, Tip.y, Tip.z};
        float const Excess = TipDistance - Reach;

        // 0x00513010..0x005130e6: past the shell (TEST AH,0x41 / JNE = not greater skips,
        // NaN included) the end stops on the shell, Source + ToLoot * (Reach / |ToLoot|) --
        // unscaled when |ToLoot|^2 compares equal to 0.0f (TEST AH,0x44 / JNP). z keeps its
        // product on the FPU into the sum; x and y are rounded first.
        if (Excess > 0.0f)
        {
            double const LootDistanceSq = static_cast<double>(ToLoot[1]) * ToLoot[1] +
                                          static_cast<double>(ToLoot[2]) * ToLoot[2] +
                                          static_cast<double>(ToLoot[0]) * ToLoot[0];
            float ShellX = ToLoot[0];
            float ShellY = ToLoot[1];
            double ShellZ = ToLoot[2];
            if (LootDistanceSq != 0.0)
            {
                double const Scale = Reach / sqrt(LootDistanceSq);
                ShellX = static_cast<float>(ToLoot[0] * Scale);
                ShellY = static_cast<float>(ToLoot[1] * Scale);
                ShellZ = ToLoot[2] * Scale;
            }
            EndPoint[0] = ShellX + Source.x;
            EndPoint[1] = ShellY + Source.y;
            EndPoint[2] = static_cast<float>(ShellZ + Source.z);
        }

        // 0x005130ea..0x00513124: FUN_0053ebe0(0.5) twice; the first draw moves y, the second
        // (still on the FPU) moves x.
        float const JitterY = static_cast<float>(RandomScaled(kEndJitter));
        double const JitterX = RandomScaled(kEndJitter);
        EndPoint[0] = static_cast<float>(EndPoint[0] + JitterX);
        EndPoint[1] = EndPoint[1] + JitterY;

        // 0x00513126..0x0051317f: the straight beam, source -> end, width 5.0f (0x40a00000).
        Line.bCurved = 0;
        Line.Start[0] = Source.x;
        Line.Start[1] = Source.y;
        Line.Start[2] = Source.z;
        Line.End[0] = EndPoint[0];
        Line.End[1] = EndPoint[1];
        Line.End[2] = EndPoint[2];
        Line.Width = kBeamWidth;
        Win3DDrawFuzzyLine(&Line);

        // 0x00513184..0x00513196: the same Excess test again for the sparks.
        if (Excess > 0.0f)
        {
            // 0x0051319c..0x005131bb: the spread Excess / Radius * 0.8 pi (DAT_005db708, a
            // double) and LookMatrixYup(ToLoot).
            float const Spread = static_cast<float>(static_cast<double>(Excess) / Radius *
                                                    kSparkSpreadScale);
            Vector LookDir;
            LookDir.x = ToLoot[0];
            LookDir.y = ToLoot[1];
            LookDir.z = ToLoot[2];
            Matrix const Look = LookMatrixYup(LookDir);
            FMatrix LookMatrix;
            for (int Row = 0; Row < 3; ++Row)
            {
                for (int Col = 0; Col < 3; ++Col)
                    LookMatrix.D[Row][Col] = Look.E[Row][Col];
            }

            // 0x005131c1..0x00513211: the spark centre, 0.3f (DAT_005d3e08) back along the
            // beam from the loot's position; x and y subtract the unrounded product, z the
            // stored one.
            float const Centre[3] = {
                static_cast<float>(LootPos.x - static_cast<double>(TipDir[0]) * kSparkPullBack),
                static_cast<float>(LootPos.y - static_cast<double>(TipDir[1]) * kSparkPullBack),
                LootPos.z - TipDir[2] * kSparkPullBack};

            // 0x00513215..0x00513223: sin kept as a double, cos stored as a float.
            double const SinSpread = sin(static_cast<double>(Spread));
            float const CosSpread = static_cast<float>(cos(static_cast<double>(Spread)));

            for (int Spark = 0; Spark < 5; ++Spark)
            {
                // 0x00513227..0x00513255: a direction on the cone about the look axis,
                // theta = FUN_0053ebe0(2 pi) (0x40c90fdb).
                double const Theta = RandomScaled(kTwoPi);
                FVector ConeDir;
                ConeDir.X = static_cast<float>(cos(Theta) * SinSpread);
                ConeDir.Y = static_cast<float>(sin(Theta) * SinSpread);
                ConeDir.Z = CosSpread;

                // 0x00513259..0x0051327d: DAT_006751b0 (FUN_005b73e0 on first use) slot
                // +0x2c, TransformMatrix.
                if (G_TractorBeamMath == nullptr)
                    G_TractorBeamMath = Acquire3DMathEngine();
                FVector Local;
                G_TractorBeamMath->TransformMatrix(Local, LookMatrix, ConeDir);

                // 0x00513280..0x0051333a: the arc from the beam end to Centre + Local, bent
                // about Centre, width 1.0f.
                Line.End[0] = Local.X + Centre[0];
                Line.End[1] = Local.Y + Centre[1];
                Line.End[2] = Local.Z + Centre[2];
                Line.Control[0] = Centre[0];
                Line.Control[1] = Centre[1];
                Line.Control[2] = Centre[2];
                Line.Start[0] = EndPoint[0];
                Line.Start[1] = EndPoint[1];
                Line.Start[2] = EndPoint[2];
                Line.Width = 1.0f;
                Line.bCurved = 1;
                Win3DDrawFuzzyLine(&Line);
            }
        }

        // 0x00513350..0x0051335c.
        ++Drawn;
    }

    // 0x00513375..0x00513396: depth write back on, culling on, blending off; true.
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DDisable(kWin3DBlend);
    return 1;
}

FCliTractorBeam::FCliTractorBeam() : FEffectBase(kTractorBeamEffectType)
{
    Owner = nullptr;
    Color[0] = 0.0f;
    Color[1] = 0.0f;
    Color[2] = 0.0f;
    LineShape = nullptr;
    BulbShape = nullptr;
}

FCliTractorBeam* CliTractorBeamCreate()
{
    // 0x0052fa2b..0x0052fa66.
    FCliTractorBeam* const Beam = static_cast<FCliTractorBeam*>(EffectAlloc(0x84u));
    if (Beam == nullptr)
        return nullptr;
    ::new (Beam) FCliTractorBeam();
    return Beam;
}

unsigned char CliTractorBeamInit(FCliTractorBeam* This, FClientEquip* Owner)
{
    // FUN_00512be0 (RET 4). 0x00512bec: +0x6c = Owner.
    This->Owner = Owner;

    // 0x00512bef..0x00512c6c: the CliEQTractor ([[[Owner+4]+0xc]+4], no tests) colour bytes
    // +0x54 / +0x55 / +0x56 (MOVZX, FILD) each times DAT_005c7f68 into +0x70 / +0x74 / +0x78.
    unsigned char const* const Comp =
        reinterpret_cast<unsigned char const*>(Owner->Equip->Arch->Companion);
    This->Color[0] = static_cast<float>(Comp[0x54]) * kByteToUnit;
    This->Color[1] = static_cast<float>(Comp[0x55]) * kByteToUnit;
    This->Color[2] = static_cast<float>(Comp[0x56]) * kByteToUnit;

    // 0x00512c6f..0x00512cf0: "line" (DAT_005db700) through the inlined ShapeDB find (FDUMP
    // cmn\ShapeDB.h line 0xf2 on a miss) into +0x7c; null returns false.
    This->LineShape = G_EffectShapeDB.Find("line");
    if (This->LineShape == nullptr)
        return 0;

    // 0x00512cf3..0x00512d85: "bulb" (DAT_005db6f8) the same way into +0x80.
    This->BulbShape = G_EffectShapeDB.Find("bulb");
    if (This->BulbShape == nullptr)
        return 0;

    // 0x00512d88..0x00512d91: FUN_004f7af0 (link into the type bucket), true.
    EffectTypeListLink(This);
    return 1;
}
