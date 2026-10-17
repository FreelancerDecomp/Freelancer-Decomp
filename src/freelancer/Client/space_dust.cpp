// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// SpaceDust -- retail Client\Space\SpaceDust.cpp, freelancer.exe 0x0054bda0..0x0054c361.
// Listing: LAB_0054bda0 / LAB_0054bdb0 (static init), FUN_0054bdc0, FUN_0054bde0, FUN_0054be20,
//          FUN_0054beb0, FUN_0054c320.

#include "space_dust.h"

#include "effect_type_list.h"
#include "game_options.h"
#include "particle_effect.h"
#include "perf_options.h"
#include "star_sphere.h"
#include "vis_effect_arch.h"

#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"
#include "dacom.h"
#include "x86math.h"

namespace
{
    char const kSpaceDustCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceDust.cpp";

    // Retail constants.
    float const kDustDetailThreshold = 0.75f;           // DAT_005c8930
    float const kDustIntensityPerParticle = 0.002f;     // DAT_005d3e94
    float const kOneF = 1.0f;                           // DAT_005c75dc
    double const kFadeCutoff = 1e-5;                    // DAT_005c75f0
    double const kOneD = 1.0;                           // DAT_005c89b8
    double const kSystemDustIntensityScale = 0.002;     // DAT_005e08f8
    int const kDefaultZoneDustParticles = 0x32;         // 0054c013
    unsigned const kPropDustZoneMask = 0x200000;        // 0054bfc0, System slot +0x30 mask

    // DAT_00614084 (.data, initial 0xffffffc4, read only at 0054bf1c): the dust frame sits
    // this far along the camera's Z axis.
    int const kSpaceDustOffsetZ = -60;

    CacheString MakeClearedCacheString()
    {
        CacheString Result;
        Result.clear();
        return Result;
    }

    // LAB_0054bda0 / LAB_0054bdb0 -- static init: CacheString::clear on each name.
    CacheString G_SystemDust = MakeClearedCacheString(); // DAT_00679794
    CacheString G_ZoneDust = MakeClearedCacheString();   // DAT_00679790

    I3DMathEngine* G_SpaceDustMath = nullptr; // DAT_006797a4 -- FUN_0054c320's lazy engine

    int G_SystemDustAtFullDetail = 0;         // DAT_006797a8 (0x50 after [Dust])
    int G_SystemDustAtZeroDetail = 0;         // DAT_006797ac (0x1e after [Dust])
    int G_ZoneDustParticles = 0;              // DAT_006797b0
    int G_FadingDustParticles = 0;            // DAT_006797b4
    FParticleEffect* G_SystemDustEffect = nullptr; // DAT_006797b8
    FParticleEffect* G_ZoneDustEffect = nullptr;   // DAT_006797bc
    FParticleEffect* G_FadingDustEffect = nullptr; // DAT_006797c0 -- previous zone's, fading

    // ParticleEffect vtable slots called here (PTR_FUN_005d9ad4): +0x00 Start, +0x04 the
    // death arm (LAB_004f84d0), +0x0c SetIntensity (LAB_004f8c20). Retail calls them
    // virtually; so does the port.

    // Camera vt+0x10: __stdcall(this), returns the camera transform.

    void DustArmDeath(FParticleEffect* Effect)
    {
        Effect->Kill();
    }

    // FUN_0054bdc0 (cdecl): death-arm and release *Slot, then null it.
    void ReleaseDustEffect(FParticleEffect** Slot)
    {
        if (*Slot != nullptr)
        {
            DustArmDeath(*Slot);
            ParticleEffectRelease(*Slot); // FUN_004f7a90
            *Slot = nullptr;
        }
    }

    // FUN_0054c320 (thiscall on the camera transform, RET 8): Out = CamXform * Offset through
    // the file's own lazily acquired 3DMathEngine (FUN_005b73e0), slot +0x1c.
    void ComposeDustXform(FTransform const* CamXform, FTransform* Out, FTransform const* Offset)
    {
        if (G_SpaceDustMath == nullptr)
            G_SpaceDustMath = Acquire3DMathEngine();
        FTransform Local;
        G_SpaceDustMath->MulTransform(Local, *CamXform, *Offset);
        *Out = Local;
    }

    // 0054c0a1..0054c0f2 / 0054c299..0054c311: allocate (FUN_004f79a0(0x84) + FUN_004f8060(0)),
    // then start it on the archetype named Name (FUN_0040f080(&CreateID(Name))), or FDUMP.
    void CreateDustEffect(FParticleEffect** Slot, char const* Name)
    {
        *Slot = ParticleEffectCreate();
        unsigned const NicknameId = CreateID(Name);
        FEffectArch* const Arch = FindEffectArch(&NicknameId);
        if (Arch != nullptr)
        {
            (*Slot)->Start(Arch);
        }
        else
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: no archetype found for effect named \"%s\" in space dust",
                  kSpaceDustCppPath, 0x49, Name);
        }
    }

    // 0054c146..0054c174 / 0054c186..0054c1c8 / 0054c22b..0054c283: place the effect on the
    // dust frame, set its intensity, mark it (+0x08 |= 1) and link it (FUN_004f7af0).
    void PlaceDustEffect(FParticleEffect* Effect, FTransform const& DustXform, float Intensity)
    {
        Effect->LocalXform = DustXform;
        Effect->SetIntensity(Intensity);
        Effect->Flags08 = static_cast<unsigned char>(Effect->Flags08 | 1);
        EffectTypeListLink(Effect);
    }
}

void SpaceDustSetSystemDust(CacheString const* Dust)
{
    // FUN_0054bde0.
    G_SystemDust.Value = Dust->Value;
    G_SystemDustAtFullDetail = 0x50;
    G_SystemDustAtZeroDetail = 0x1e;
    G_ZoneDust.clear();
    G_ZoneDustParticles = 0;
    G_FadingDustParticles = 0;
}

void SpaceDustShutdown()
{
    // FUN_0054be20 -- the same release sequence as FUN_0054bdc0, inlined.
    ReleaseDustEffect(&G_FadingDustEffect);
    ReleaseDustEffect(&G_ZoneDustEffect);
    G_ZoneDust.clear();
    G_ZoneDustParticles = 0;
    G_FadingDustParticles = 0;
    ReleaseDustEffect(&G_SystemDustEffect);
    G_SystemDust.clear();
    G_SystemDustAtFullDetail = 0;
}

void SpaceDustUpdate(Universe::ISystem const* System, void* Camera)
{
    // FUN_0054beb0.

    // 0054beb7..0054bf19: background option off -- drop every dust effect (no early return).
    if (GetGameOption(kGameOptBackgroundGroup) == 0 &&
        (G_SystemDustEffect != nullptr || G_ZoneDustEffect != nullptr ||
         G_FadingDustEffect != nullptr))
    {
        ReleaseDustEffect(&G_FadingDustEffect);
        ReleaseDustEffect(&G_ZoneDustEffect);
        G_ZoneDust.clear();
        G_ZoneDustParticles = 0;
        G_FadingDustParticles = 0;
        ReleaseDustEffect(&G_SystemDustEffect);
    }

    // 0054bf1c..0054bf79: DustXform = camera transform * (identity, (0, 0, -60)).
    FTransform Offset;
    Win3DIdentityRotation3x3(&Offset.Rotation.D[0][0]); // FUN_004215b0
    Offset.Translation.X = 0.0f;
    Offset.Translation.Y = 0.0f;
    Offset.Translation.Z = static_cast<float>(kSpaceDustOffsetZ);
    FTransform const* const CamXform =
        (FTransform const*)((ICamera*)Camera)->GetOrientation();
    FTransform DustXform;
    ComposeDustXform(CamXform, &DustXform, &Offset);
    Vector const& DustPos = *reinterpret_cast<Vector const*>(&DustXform.Translation);

    // 0054bf7e..0054bf95: TEST AH,0x41 / JNZ -- the zone dust runs only when detail > 0.75.
    if (GetPerfSpaceDustDetail() > kDustDetailThreshold) // DAT_00614080
    {
        CacheString Name;
        Name.clear();
        Name.clear();
        int Particles = 0;
        float Blend = 1.0f;
        if (System != nullptr)
        {
            Universe::IZone const* const Zone = System->GetSmallestZoneWithPropertiesAt(&DustPos, kPropDustZoneMask);
            if (Zone != nullptr)
            {
                if (!Zone->GetSpacedust(Name.Value, Particles))
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kSpaceDustCppPath, 0xa8,
                          "zone flagged as PROP_DUST_ZONE but has no dust");
                }
                if (Particles == 0)
                    Particles = kDefaultZoneDustParticles;
                Blend = Zone->GetImmersion(DustPos);
            }
        }

        // 0054c02a..0054c0f2: a different zone dust name (pointer compare, as retail) moves the
        // current zone effect to the fading slot and starts the new one.
        if (Name.Value != G_ZoneDust.Value)
        {
            G_ZoneDust.Value = Name.Value;
            if (G_FadingDustEffect != nullptr)
            {
                DustArmDeath(G_FadingDustEffect);
                ParticleEffectRelease(G_FadingDustEffect);
            }
            G_FadingDustEffect = G_ZoneDustEffect;
            G_FadingDustParticles = G_ZoneDustParticles;
            G_ZoneDustEffect = nullptr;
            if (Name.Value[0] != '\0')
                CreateDustEffect(&G_ZoneDustEffect, Name.Value);
        }

        // 0054c0f8..0054c144: fading effect -- dies once its level reaches 1e-5.
        G_ZoneDustParticles = Particles;
        if (G_FadingDustEffect != nullptr)
        {
            double const Fade =
                (static_cast<double>(G_FadingDustParticles) *
                 static_cast<double>(kDustIntensityPerParticle)) *
                (static_cast<double>(kOneF) - static_cast<double>(Blend));
            // TEST AH,0x41 / JP at 0054c12c: the effect stays when Fade > 1e-5 or unordered.
            if (!(Fade <= kFadeCutoff))
            {
                PlaceDustEffect(G_FadingDustEffect, DustXform, static_cast<float>(Fade));
            }
            else
            {
                DustArmDeath(G_FadingDustEffect);
                ParticleEffectRelease(G_FadingDustEffect);
                G_FadingDustEffect = nullptr;
            }
        }

        // 0054c179..0054c1c8: current zone effect.
        if (G_ZoneDustEffect != nullptr)
        {
            float const Level = static_cast<float>(
                (static_cast<double>(G_ZoneDustParticles) *
                 static_cast<double>(kDustIntensityPerParticle)) *
                static_cast<double>(Blend));
            PlaceDustEffect(G_ZoneDustEffect, DustXform, Level);
        }
    }
    else
    {
        // 0054c1cf..0054c21c.
        ReleaseDustEffect(&G_FadingDustEffect);
        ReleaseDustEffect(&G_ZoneDustEffect);
        G_ZoneDust.clear();
        G_ZoneDustParticles = 0;
        G_FadingDustParticles = 0;
    }

    // 0054c222..0054c311: the system's own dust.
    if (G_SystemDustEffect != nullptr)
    {
        double const Detail = static_cast<double>(GetPerfSpaceDustDetail()); // DAT_00614080
        float const Level = static_cast<float>(
            ((kOneD - Detail) * static_cast<double>(G_SystemDustAtZeroDetail) +
             static_cast<double>(G_SystemDustAtFullDetail) * Detail) *
            kSystemDustIntensityScale);
        PlaceDustEffect(G_SystemDustEffect, DustXform, Level);
        return;
    }
    if (G_SystemDust.Value[0] != '\0')
        CreateDustEffect(&G_SystemDustEffect, G_SystemDust.Value);
}
