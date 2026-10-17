// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Port-only (no retail counterpart): the ray-marched volumetric nebula. See nebula_volume.h and
// docs/re/rp9.md, "Volumetric nebula".
//
// Frame flow (space_scene_obj_draw.cpp, SpaceObjDrawPass):
//   SelectFieldControllers -> NebulaVolume::BeginFrame: the nebulae drawn this frame (inside
//                              and, with exterior, within exterior_max_distance), nearest first;
//                              then the lightning (UpdateLightning: strikes placed in their
//                              clouds, envelopes, the ship light)
//   far plane               -> NebulaVolume::InsideFarPlane (fully inside: pushed out to where
//                              the haze has hidden everything)
//   star sphere             -> drawn also when fully inside (ShowsRealBackground)
//   DrainBucket2Special     -> far ZItemNebula nodes: the retail exterior parts ask
//                              ReplacesExteriorBits / ReplacesFillSheets; a nebula beyond the
//                              scene's far plane is ray-marched right there (DrawBackground,
//                              painter order with the far solars, no depth test)
//   DrainMeshAndAsteroids   -> the retail gates ask IsActive*/Replaces* (fog overlay quad,
//                              per-object D3D fog, interior puff panels, the controller's bits
//                              and fill sheets); the asteroid fields ask AsteroidSpawnReach
//   NebulaVolume::Draw      -> per remaining nebula, farthest first: half-resolution ray march
//                              against the INTZ scene depth, then a depth-aware full-resolution
//                              composite onto the back buffer
//   effects (alchemy)       -> drawn over the volume, as retail draws them over its fog
//   clear colour            -> the system's space colour, not the fog colour
//                              (ShowsRealBackground)
//
// Everything the shaders see is camera-relative (system coordinates reach +-200k, too far for
// float precision); positions are subtracted in double here.
// Compiled only with FREELANCER_RP9 (src/Directory.Build.props).
#ifdef FREELANCER_RP9
#include "nebula_volume.h"

#include "cli_nebula.h"
#include "render_ex.h"
#include "star_sphere.h"

#include "DALib.h"
#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h" // ICamera
#include "common_net.h"  // Exclusion::NebulaExclusionZoneMgr
#include "common_universe.h"
#include "dacom.h"
#include "engine_light.h" // the ship light of the lightning (GameLight)
#include "render/IRenderPipeline.h"
#include "render/IRenderPipelineEx.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

// Read-only access to the nebula's exclusion zone list (befriended by
// Exclusion::NebulaExclusionZoneMgr in common_net.h).
struct FNebulaVolumeExclusionAccess
{
    static int Count(Exclusion::NebulaExclusionZoneMgr const* Mgr)
    {
        if (Mgr->Begin == nullptr)
            return 0;
        return (int)(Mgr->End - Mgr->Begin);
    }

    static Exclusion::NebulaExclusionZone const* At(Exclusion::NebulaExclusionZoneMgr const* Mgr,
                                                    int Index)
    {
        return Mgr->Begin[Index];
    }
};

namespace
{
    const char kNebulaVolumeSource[] = "nebula_volume.cpp";
    // DATA_EXTRA-relative (RenderEx::GetExtraData). Shared helpers: SHADERS\nebula_volume.hlsli.
    const char kNebulaVolumeShader[] = "SHADERS\\nebula_volume.hlsl";
    const char kNebulaCompositeShader[] = "SHADERS\\nebula_composite.hlsl";

    // Must match nebula_volume.hlsl: register layout (c0..c102), loop bounds.
    const int kMaxExclusions = 8;
    const int kMaxSteps = 128;
    const int kMaxShadowSteps = 6;     // NEBULA_SHADOW_STEPS
    const int kMaxShadowStepsHalf = 3; // NEBULA_SHADOW_STEPS_HALF
    const int kMaxPointLights = 4;     // NEBULA_MAX_POINT_LIGHTS
    const int kMaxFarSpheres = 4;      // FarSphere[]
    const int kMaxStarGlows = 3;       // StarGlow[]
    const int kRaymarchRegisterCount = 103;
    const int kRegShadowStep = 29;
    const int kRegExclusionCentre = 37;
    const int kRegExclusionRow0 = 45;
    const int kRegExclusionRow1 = 53;
    const int kRegExclusionRow2 = 61;
    const int kRegLightPos = 69;
    const int kRegLightColour = 73;
    const int kRegLod = 77;
    const int kRegExterior = 78;
    const int kRegBudget = 79;
    const int kRegMacroShadow = 80;
    const int kRegShadowStepHalf = 81;
    const int kRegLightShape = 84;
    const int kRegLightMore = 85;
    const int kRegLightExtra = 86;
    const int kRegPuffColourDelta = 87;
    const int kRegShell = 88;
    const int kRegHalo = 89;
    const int kRegShellNoise = 90;
    const int kRegShellMore = 91;
    const int kRegKeyLocal = 92;
    const int kRegFarSphere = 93;
    const int kRegStarGlow = 97;
    const int kRegStarDistance = 100;
    const int kRegStarWeight = 101;
    const int kRegAsteroidFade = 102;
    const int kCompositeRegisterCount = 4;
    // Shadow steps grow by this ratio (short near the sample, long toward the light).
    const double kShadowStepGrowth = 1.6;
    // Nebulae drawn per frame at most ([Nebula] max_nebulae is clamped to this).
    const int kMaxNebulae = 4;

    // Pixel shader instruction slots the volume needs (FRPExCaps::MaxPixelShaderSlots). PSRaymarch
    // measured with fxc /T ps_3_0 (June 2010 SDK, the compiler generation of RP9's
    // D3DXCompileShader): 1327 slots (24 texture, 1303 arithmetic) with the lightning point
    // lights (NEBULA_POINT_LIGHTS 1; 937 without). PSComposite is 179. Required = the larger
    // count plus ~25% headroom for edits; re-measure and raise this when the shader grows. The
    // compiler does not check any slot limit -- the device refuses a shader above the driver's
    // limit (512 is only the Shader Model 3 minimum; an RTX 5080 reports 4096). The shader also
    // uses all 32 temp registers of ps_3_0 (see its header).
    const unsigned long kRaymarchPsSlotsRequired = 1660;

    // Executed instructions of PSRaymarch, read off the fxc listing (dynamic loops count every
    // iteration): one march step inside a cloud in the outer shell (with the displacement
    // lookup) with the detail lookup, the full key-light shadow, the large-scale haze shadow
    // and the light-span test ~295; each exclusion zone tested in a step ~15 (unrolled, no
    // cmp-chain array indexing); each point light whose sphere the step is in ~65 with the
    // midpoint cloud sample and the core test, plus ~15 per step for the light block itself;
    // the per-pixel setup (~540 with 8 exclusion zones in the span test and the far spheres) and
    // the cores, haze tail, fading-asteroid tail and star glows after the march (~240) stay
    // under kExecutedReserve. The shader caps its step count
    // per pixel so that steps * (step + lights * light + zones * zone) stays inside the
    // device's MaxPixelShaderInstructionsExecuted minus the reserve: inside with 4 lights and
    // 8 zones on the ray that is 86 steps (65535 - 1000) / (300 + 4 * 80 + 8 * 16).
    const float kExecutedPerStep = 300.0f;
    const float kExecutedPerLightStep = 80.0f;
    const float kExecutedPerExclusionStep = 16.0f;
    const float kExecutedReserve = 1000.0f;

    // Lightning strikes alive at once over all drawn nebulae (each is a point light of the
    // nebula it belongs to; the shader takes kMaxPointLights per nebula). Sub-pulses per strike.
    const int kMaxStrikes = 4;
    const int kMaxStrikePulses = 4;
    // Retail flashes every [Clouds] puff on its own lightning_gap timer (texture_panels.cpp);
    // the volume has no puffs and counts the gap as this many independent sources.
    const double kCloudLightningSources = 4.0;
    // Retail's [DynamicLightning] light: a directional GameLight of range 500000 (cli_nebula.cpp,
    // FNebulaEffect::Load); the volume's own ship light is built the same way.
    const float kShipLightRange = 500000.0f;

    // Noise volume: kNoiseSize^3 A8R8G8B8, tileable. R = Perlin-Worley (4 cells per tile),
    // G = Worley fBm (8 cells), B = Worley fBm (16 cells), A = Perlin (4 cells, colour).
    const unsigned long kNoiseSize = 64;
    const double kBaseCellsPerTile = 4.0;   // R: the cloud base shape
    const double kDetailCellsPerTile = 8.0; // G (B twice as fine): the edge erosion
    const double kPi = 3.14159265358979323846;

    // ---- settings -------------------------------------------------------------------------

    // [Nebula] (and [NebulaOverride]) of DATA_EXTRA/VOLUMETRIC/volumetric.ini. Every key is a float
    // (two are three floats) so one table parses both sections; the ini documents each key.
    struct FNebulaVolumeSettings
    {
        float Enabled;
        float ResolutionScale;
        float StepCount;
        float StepDistribution;
        float Jitter;
        float MaxMarchDistance;
        float MarchEndTransmittance;
        float AutoMarchFogDistances;
        float EdgeSoftness;
        float DensityScale;
        float HazeTransmittanceAtFogDistance;
        float CloudDensity;
        float CloudScaleM;
        float CloudScaleFogDistances;
        float Coverage;
        float CloudSoftness;
        float LayerStretch;
        float DetailScale;
        float Erosion;
        float Wispiness;
        float Warp;
        float DriftScale;
        float CloudNearFadeScale;
        float CloudBrightness;
        float HazeAlbedo;
        float Emission;
        float HazeEmission;
        float AmbientScale;
        float AoStrength;
        float AoDistance;
        float KeyLightSource;
        float KeyLightIntensity;
        float KeyLightDir[3];
        float KeyLightColour[3];
        float KeyLightFixedIntensity;
        float PhaseG;
        float PhaseBackG;
        float PhaseBackWeight;
        float MsStrength;
        float MsExtinction;
        float MsPhase;
        float PowderStrength;
        float ShadowSteps;
        float ShadowDistance;
        float ShadowDensity;
        float ExclusionDensityScale;
        float HazeTail;
        float RealBackground;
        float ExtendFarPlane;
        float Exposure;
        float Tonemap;
        float TonemapWhite;
        float UpsampleDepthThreshold;
        float ReplaceObjectFog;
        float ReplaceFogOverlay;
        float ReplaceClouds;
        // Phase 3: from outside, several nebulae, level of detail, asteroid spawn.
        float Exterior;
        float ExteriorMaxDistance;
        float ExteriorFadeDistance;
        float MaxNebulae;
        float ReplaceExteriorBits;
        float ReplaceFillSheets;
        float LodStartDistance;
        float LodEndDistance;
        float LodMinStepFraction;
        float LodShadowSwitch;
        float LodDetailFade;
        float ExteriorShadowDensity;
        float ExteriorShadowFadeIn;
        float AsteroidSpawn;
        float AsteroidSpawnTransmittance;
        float AsteroidMaxSpawnDistance;
        float AsteroidFadeFraction;
        // Lightning: strikes inside the clouds as point lights, the ship light.
        float LightningEnabled;
        float LightningIntensityScale;
        float LightningRadius;
        float LightningCoreSize;
        float LightningCoreEmission;
        float LightningCoreGlowSize;
        float LightningFrequencyScale;
        float LightningDurationScale;
        float LightningBurstChance;
        float LightningMinDistance;
        float LightningMaxDistance;
        float LightningFarChance;
        float LightningFarDistance;
        float LightningViewBias;
        float LightningCandidates;
        float LightningMinDensity;
        float LightningShadow;
        float LightningShadowDensity;
        float LightningHazeScale;
        float LightningPhaseG;
        float LightningFlicker;
        float LightningPulseJitter;
        float LightningColour[3];
        float LightningHazeFlash;
        float LightningLightsShips;
        float LightningShipIntensity;
        float LightningShipDistance;
        // The outer shell and the halo: a soft, irregular, see-through edge from outside.
        float ShellWidth;
        float ShellNoise;
        float ShellNoiseScale;
        float ShellCloudFalloff;
        float HaloExtent;
        float HaloDensity;
        float ExteriorShellDensity;
        float FarCloudSoftening;
        float FarDetailFade;
        // Objects without scene depth: how much of the scene a star glow is at its centre (the
        // shader's StarWeight; 0 = star glows fogged by the whole ray, as before).
        float StarGlowWeight;
    };

    struct FSettingKey
    {
        const char* Key;
        float FNebulaVolumeSettings::*Member;
    };

    struct FSettingVectorKey
    {
        const char* Key;
        float (FNebulaVolumeSettings::*Member)[3];
    };

    const FSettingKey kSettingKeys[] = {
        {"enabled", &FNebulaVolumeSettings::Enabled},
        {"resolution_scale", &FNebulaVolumeSettings::ResolutionScale},
        {"step_count", &FNebulaVolumeSettings::StepCount},
        {"step_distribution", &FNebulaVolumeSettings::StepDistribution},
        {"jitter", &FNebulaVolumeSettings::Jitter},
        {"max_march_distance", &FNebulaVolumeSettings::MaxMarchDistance},
        {"march_end_transmittance", &FNebulaVolumeSettings::MarchEndTransmittance},
        {"auto_march_fog_distances", &FNebulaVolumeSettings::AutoMarchFogDistances},
        {"edge_softness", &FNebulaVolumeSettings::EdgeSoftness},
        {"density_scale", &FNebulaVolumeSettings::DensityScale},
        {"haze_transmittance_at_fog_distance",
         &FNebulaVolumeSettings::HazeTransmittanceAtFogDistance},
        {"cloud_density", &FNebulaVolumeSettings::CloudDensity},
        {"cloud_scale_m", &FNebulaVolumeSettings::CloudScaleM},
        {"cloud_scale_fog_distances", &FNebulaVolumeSettings::CloudScaleFogDistances},
        {"coverage", &FNebulaVolumeSettings::Coverage},
        {"cloud_softness", &FNebulaVolumeSettings::CloudSoftness},
        {"layer_stretch", &FNebulaVolumeSettings::LayerStretch},
        {"detail_scale", &FNebulaVolumeSettings::DetailScale},
        {"erosion", &FNebulaVolumeSettings::Erosion},
        {"wispiness", &FNebulaVolumeSettings::Wispiness},
        {"warp", &FNebulaVolumeSettings::Warp},
        {"drift_scale", &FNebulaVolumeSettings::DriftScale},
        {"cloud_near_fade_scale", &FNebulaVolumeSettings::CloudNearFadeScale},
        {"cloud_brightness", &FNebulaVolumeSettings::CloudBrightness},
        {"haze_albedo", &FNebulaVolumeSettings::HazeAlbedo},
        {"emission", &FNebulaVolumeSettings::Emission},
        {"haze_emission", &FNebulaVolumeSettings::HazeEmission},
        {"ambient_scale", &FNebulaVolumeSettings::AmbientScale},
        {"ao_strength", &FNebulaVolumeSettings::AoStrength},
        {"ao_distance", &FNebulaVolumeSettings::AoDistance},
        {"key_light_source", &FNebulaVolumeSettings::KeyLightSource},
        {"key_light_intensity", &FNebulaVolumeSettings::KeyLightIntensity},
        {"key_light_fixed_intensity", &FNebulaVolumeSettings::KeyLightFixedIntensity},
        {"phase_g", &FNebulaVolumeSettings::PhaseG},
        {"phase_back_g", &FNebulaVolumeSettings::PhaseBackG},
        {"phase_back_weight", &FNebulaVolumeSettings::PhaseBackWeight},
        {"ms_strength", &FNebulaVolumeSettings::MsStrength},
        {"ms_extinction", &FNebulaVolumeSettings::MsExtinction},
        {"ms_phase", &FNebulaVolumeSettings::MsPhase},
        {"powder_strength", &FNebulaVolumeSettings::PowderStrength},
        {"shadow_steps", &FNebulaVolumeSettings::ShadowSteps},
        {"shadow_distance", &FNebulaVolumeSettings::ShadowDistance},
        {"shadow_density", &FNebulaVolumeSettings::ShadowDensity},
        {"exclusion_density_scale", &FNebulaVolumeSettings::ExclusionDensityScale},
        {"haze_tail", &FNebulaVolumeSettings::HazeTail},
        {"real_background", &FNebulaVolumeSettings::RealBackground},
        {"extend_far_plane", &FNebulaVolumeSettings::ExtendFarPlane},
        {"exposure", &FNebulaVolumeSettings::Exposure},
        {"tonemap", &FNebulaVolumeSettings::Tonemap},
        {"tonemap_white", &FNebulaVolumeSettings::TonemapWhite},
        {"upsample_depth_threshold", &FNebulaVolumeSettings::UpsampleDepthThreshold},
        {"replace_object_fog", &FNebulaVolumeSettings::ReplaceObjectFog},
        {"replace_fog_overlay", &FNebulaVolumeSettings::ReplaceFogOverlay},
        {"replace_clouds", &FNebulaVolumeSettings::ReplaceClouds},
        {"exterior", &FNebulaVolumeSettings::Exterior},
        {"exterior_max_distance", &FNebulaVolumeSettings::ExteriorMaxDistance},
        {"exterior_fade_distance", &FNebulaVolumeSettings::ExteriorFadeDistance},
        {"max_nebulae", &FNebulaVolumeSettings::MaxNebulae},
        {"replace_exterior_bits", &FNebulaVolumeSettings::ReplaceExteriorBits},
        {"replace_fill_sheets", &FNebulaVolumeSettings::ReplaceFillSheets},
        {"lod_start_distance", &FNebulaVolumeSettings::LodStartDistance},
        {"lod_end_distance", &FNebulaVolumeSettings::LodEndDistance},
        {"lod_min_step_fraction", &FNebulaVolumeSettings::LodMinStepFraction},
        {"lod_shadow_switch", &FNebulaVolumeSettings::LodShadowSwitch},
        {"lod_detail_fade", &FNebulaVolumeSettings::LodDetailFade},
        {"exterior_shadow_density", &FNebulaVolumeSettings::ExteriorShadowDensity},
        {"exterior_shadow_fade_in", &FNebulaVolumeSettings::ExteriorShadowFadeIn},
        {"asteroid_spawn", &FNebulaVolumeSettings::AsteroidSpawn},
        {"asteroid_spawn_transmittance", &FNebulaVolumeSettings::AsteroidSpawnTransmittance},
        {"asteroid_max_spawn_distance", &FNebulaVolumeSettings::AsteroidMaxSpawnDistance},
        {"asteroid_fade_fraction", &FNebulaVolumeSettings::AsteroidFadeFraction},
        {"lightning_enabled", &FNebulaVolumeSettings::LightningEnabled},
        {"lightning_intensity_scale", &FNebulaVolumeSettings::LightningIntensityScale},
        {"lightning_radius", &FNebulaVolumeSettings::LightningRadius},
        {"lightning_core_size", &FNebulaVolumeSettings::LightningCoreSize},
        {"lightning_core_emission", &FNebulaVolumeSettings::LightningCoreEmission},
        {"lightning_core_glow_size", &FNebulaVolumeSettings::LightningCoreGlowSize},
        {"lightning_frequency_scale", &FNebulaVolumeSettings::LightningFrequencyScale},
        {"lightning_duration_scale", &FNebulaVolumeSettings::LightningDurationScale},
        {"lightning_burst_chance", &FNebulaVolumeSettings::LightningBurstChance},
        {"lightning_min_distance", &FNebulaVolumeSettings::LightningMinDistance},
        {"lightning_max_distance", &FNebulaVolumeSettings::LightningMaxDistance},
        {"lightning_far_chance", &FNebulaVolumeSettings::LightningFarChance},
        {"lightning_far_distance", &FNebulaVolumeSettings::LightningFarDistance},
        {"lightning_view_bias", &FNebulaVolumeSettings::LightningViewBias},
        {"lightning_candidates", &FNebulaVolumeSettings::LightningCandidates},
        {"lightning_min_density", &FNebulaVolumeSettings::LightningMinDensity},
        {"lightning_shadow", &FNebulaVolumeSettings::LightningShadow},
        {"lightning_shadow_density", &FNebulaVolumeSettings::LightningShadowDensity},
        {"lightning_haze_scale", &FNebulaVolumeSettings::LightningHazeScale},
        {"lightning_phase_g", &FNebulaVolumeSettings::LightningPhaseG},
        {"lightning_flicker", &FNebulaVolumeSettings::LightningFlicker},
        {"lightning_pulse_jitter", &FNebulaVolumeSettings::LightningPulseJitter},
        {"lightning_haze_flash", &FNebulaVolumeSettings::LightningHazeFlash},
        {"lightning_lights_ships", &FNebulaVolumeSettings::LightningLightsShips},
        {"lightning_ship_intensity", &FNebulaVolumeSettings::LightningShipIntensity},
        {"lightning_ship_distance", &FNebulaVolumeSettings::LightningShipDistance},
        {"shell_width", &FNebulaVolumeSettings::ShellWidth},
        {"shell_noise", &FNebulaVolumeSettings::ShellNoise},
        {"shell_noise_scale", &FNebulaVolumeSettings::ShellNoiseScale},
        {"shell_cloud_falloff", &FNebulaVolumeSettings::ShellCloudFalloff},
        {"halo_extent", &FNebulaVolumeSettings::HaloExtent},
        {"halo_density", &FNebulaVolumeSettings::HaloDensity},
        {"exterior_shell_density", &FNebulaVolumeSettings::ExteriorShellDensity},
        {"far_cloud_softening", &FNebulaVolumeSettings::FarCloudSoftening},
        {"far_detail_fade", &FNebulaVolumeSettings::FarDetailFade},
        {"star_glow_weight", &FNebulaVolumeSettings::StarGlowWeight},
    };

    const FSettingVectorKey kSettingVectorKeys[] = {
        {"key_light_dir", &FNebulaVolumeSettings::KeyLightDir},
        {"key_light_colour", &FNebulaVolumeSettings::KeyLightColour},
        {"lightning_colour", &FNebulaVolumeSettings::LightningColour},
    };

    // Tuned so that Badlands (li01_badlands_nebula: [Fog] distance 1000, near 0, dark purple;
    // puff_radius 100; a 55 x 20 x 35 km ellipsoid) reads as lit cloud masses in a thin haze.
    FNebulaVolumeSettings DefaultSettings()
    {
        FNebulaVolumeSettings S;
        S.Enabled = 1.0f;
        S.ResolutionScale = 0.5f;
        S.StepCount = 96.0f;
        S.StepDistribution = 1.5f;
        S.Jitter = 1.0f;
        S.MaxMarchDistance = 0.0f;
        S.MarchEndTransmittance = 0.02f;
        S.AutoMarchFogDistances = 3.0f;
        S.EdgeSoftness = 0.08f;
        S.DensityScale = 1.0f;
        S.HazeTransmittanceAtFogDistance = 0.5f;
        S.CloudDensity = 6.0f;
        S.CloudScaleM = 0.0f;
        S.CloudScaleFogDistances = 2.5f;
        S.Coverage = 0.4f;
        S.CloudSoftness = 0.6f;
        S.LayerStretch = 1.6f;
        S.DetailScale = 4.0f;
        S.Erosion = 0.5f;
        S.Wispiness = 0.35f;
        S.Warp = 0.3f;
        S.DriftScale = 6.0f;
        S.CloudNearFadeScale = 1.0f;
        S.CloudBrightness = 1.0f;
        S.HazeAlbedo = 0.15f;
        S.Emission = 0.25f;
        S.HazeEmission = 0.6f;
        S.AmbientScale = 1.5f;
        S.AoStrength = 1.0f;
        S.AoDistance = 0.0f;
        S.KeyLightSource = 0.0f;
        S.KeyLightIntensity = 3.0f;
        S.KeyLightDir[0] = 0.3f;
        S.KeyLightDir[1] = 0.8f;
        S.KeyLightDir[2] = 0.5f;
        S.KeyLightColour[0] = 0.0f;
        S.KeyLightColour[1] = 0.0f;
        S.KeyLightColour[2] = 0.0f;
        S.KeyLightFixedIntensity = 1.5f;
        S.PhaseG = 0.6f;
        S.PhaseBackG = 0.3f;
        S.PhaseBackWeight = 0.2f;
        S.MsStrength = 0.5f;
        S.MsExtinction = 0.35f;
        S.MsPhase = 0.5f;
        S.PowderStrength = 0.6f;
        S.ShadowSteps = 6.0f;
        S.ShadowDistance = 0.0f;
        S.ShadowDensity = 1.0f;
        S.ExclusionDensityScale = 1.0f;
        S.HazeTail = 1.0f;
        S.RealBackground = 1.0f;
        S.ExtendFarPlane = 1.0f;
        S.Exposure = 1.0f;
        S.Tonemap = 2.0f;
        S.TonemapWhite = 2.0f;
        S.UpsampleDepthThreshold = 0.1f;
        S.ReplaceObjectFog = 1.0f;
        S.ReplaceFogOverlay = 1.0f;
        S.ReplaceClouds = 1.0f;
        S.Exterior = 1.0f;
        S.ExteriorMaxDistance = 80000.0f;
        S.ExteriorFadeDistance = 20000.0f;
        S.MaxNebulae = 4.0f;
        S.ReplaceExteriorBits = 1.0f;
        S.ReplaceFillSheets = 1.0f;
        S.LodStartDistance = 5000.0f;
        S.LodEndDistance = 50000.0f;
        S.LodMinStepFraction = 0.5f;
        S.LodShadowSwitch = 0.5f;
        S.LodDetailFade = 1.0f;
        S.ExteriorShadowDensity = 0.25f;
        S.ExteriorShadowFadeIn = 5000.0f;
        S.AsteroidSpawn = 1.0f;
        S.AsteroidSpawnTransmittance = 0.05f;
        S.AsteroidMaxSpawnDistance = 6000.0f;
        S.AsteroidFadeFraction = 0.25f;
        S.LightningEnabled = 1.0f;
        S.LightningIntensityScale = 20.0f;
        S.LightningRadius = 0.0f;
        S.LightningCoreSize = 0.45f;
        S.LightningCoreEmission = 2.0f;
        S.LightningCoreGlowSize = 0.04f;
        S.LightningFrequencyScale = 1.0f;
        S.LightningDurationScale = 1.0f;
        S.LightningBurstChance = 0.35f;
        S.LightningMinDistance = 400.0f;
        S.LightningMaxDistance = 6000.0f;
        S.LightningFarChance = 0.25f;
        S.LightningFarDistance = 0.0f;
        S.LightningViewBias = 0.75f;
        S.LightningCandidates = 12.0f;
        S.LightningMinDensity = 0.3f;
        S.LightningShadow = 2.0f;
        S.LightningShadowDensity = 0.25f;
        S.LightningHazeScale = 4.0f;
        S.LightningPhaseG = 0.2f;
        S.LightningFlicker = 0.5f;
        S.LightningPulseJitter = 0.1f;
        S.LightningColour[0] = 0.0f;
        S.LightningColour[1] = 0.0f;
        S.LightningColour[2] = 0.0f;
        S.LightningHazeFlash = 0.025f;
        S.LightningLightsShips = 2.0f;
        S.LightningShipIntensity = 1.5f;
        S.LightningShipDistance = 0.0f;
        S.ShellWidth = 0.35f;
        S.ShellNoise = 0.12f;
        S.ShellNoiseScale = 0.5f;
        S.ShellCloudFalloff = 1.25f;
        S.HaloExtent = 0.25f;
        S.HaloDensity = 0.12f;
        S.ExteriorShellDensity = 0.3f;
        S.FarCloudSoftening = 2.0f;
        S.FarDetailFade = 0.6f;
        S.StarGlowWeight = 2.0f;
        return S;
    }

    struct FNebulaOverride
    {
        char File[64]; // nebula ini base name, no directory, no extension
        FNebulaVolumeSettings Settings;
    };

    struct FRegisteredNebula
    {
        FNebulaEffect* Nebula;
        char File[64];
        // Lightning scheduler (UpdateLightning): the time (SecondsNow) of the next strike, < 0 =
        // not armed yet; strikes left in the current burst and where the burst happens.
        double NextStrike;
        int BurstLeft;
        double BurstPos[3];
    };

    FNebulaVolumeSettings GSettings = DefaultSettings();
    std::vector<FNebulaOverride> GOverrides;
    std::vector<FRegisteredNebula> GNebulae;
    // [Debug] nebula_view: 0 normal, 1 cloud density, 2 key-light shadow, 3 haze only,
    // 4 clouds only.
    int GNebulaView = 0;
    // [Debug] lightning_force: strikes one after another near the camera (tuning);
    // lightning_show_positions: the strikes' cores seen through everything.
    bool GbLightningForce = false;
    bool GbLightningShowPositions = false;

    // ---- GPU objects and frame state ------------------------------------------------------

    FRPExProgram* GRaymarchProgram = nullptr;
    FRPExProgram* GCompositeProgram = nullptr;
    FRPExTexture* GNoiseVolume = nullptr;
    FRPExTexture* GVolumeTarget = nullptr;
    float GVolumeTargetScale = 0.0f;
    // The target that was actually created is FP16 (HDR, tonemapped in the composite); an
    // 8-bit one gets display values from the ray march.
    bool GbVolumeTargetFloat16 = false;
    // CreateExProgram was called (the handles live until Shutdown; a reload recompiles them).
    bool GbProgramsCreated = false;
    // A program that did not compile (at creation, or a failed bind): off until the next
    // shader reload (OnProgramsReloaded).
    bool GbProgramFailed = false;
    // Noise volume / render target creation failed: not retried every frame. The target is
    // retried after a settings reload (resolution_scale may have changed).
    bool GbNoiseFailed = false;
    bool GbTargetFailed = false;
    bool GbCapsQueried = false;
    bool GbCapsOk = false;
    bool GbFloat16Target = false;
    // Histogram of the noise volume's R bytes (the cloud base shape): turns coverage (a fraction
    // of space) into the threshold the shader cuts at.
    unsigned long GBaseHistogram[256];
    bool GbBaseHistogramValid = false;
    // The noise volume's texels, kept after the upload: the lightning places its strikes in
    // the clouds by evaluating the shader's base shape on the CPU (WorldBaseCloud).
    std::vector<unsigned long> GNoiseTexels;

    // MaxPixelShaderInstructionsExecuted of the device (QueryCaps); 65535 when it reports 0.
    float GExecutedLimit = 65535.0f;

    // One nebula the volume draws this frame (BeginFrame .. Draw).
    struct FVolumeEntry
    {
        FNebulaEffect* Nebula;
        FNebulaVolumeSettings Settings; // a copy: a hot reload never runs mid-frame anyway
        float SpanScale;                // the drawn bound (halo end + shell noise) / zone radius
        float SurfaceDistance;          // camera to that bound (m), 0 inside it
        float Strength;                 // exterior fade, 1 inside and near
        float MarchCap;                 // the march length from the ray's zone entry (m)
        float AsteroidHazeReach;        // haze length to asteroid_spawn_transmittance (m)
        float AsteroidSurfaceDistance;  // camera to the shell's half-density surface (m)
        bool bInside;                   // the camera's immersion (+0x294) > 0
        bool bBackground;               // drawn in the far bucket (DrawBackground)
        bool bDrawn;                    // drawn (or tried) this frame
    };

    // Nearest first. GEntryCount > 0 is "active".
    FVolumeEntry GEntries[kMaxNebulae];
    int GEntryCount = 0;
    // Set by BeginFrame (the controller drawn from inside with real_background), kept through
    // Draw for the frame's clear colour; cleared by the next BeginFrame or a failed draw of the
    // controller.
    bool GbRealBackground = false;
    FNebulaEffect* GController = nullptr;
    // InsideFarPlane: the controller's march cap (when drawn from inside) and the farthest
    // asteroid spawn reach any drawn nebula can ask for.
    float GControllerMarchCap = 0.0f;
    float GMaxAsteroidReach = 0.0f;

    // What the frame drew that the scene depth does not hold (NebulaVolume::Note*), collected
    // from BeginFrame on; each nebula pass uses what was drawn before it (a far-bucket node only
    // what the bucket drew behind it). World positions; the camera position of the frame ranks
    // the spheres. Fixed arrays, no allocation.
    struct FFarSphereNote
    {
        double Centre[3];
        float Radius;
        float Angular; // Radius / distance: the largest kFarSphere ones are kept
    };
    struct FStarGlowNote
    {
        double Centre[3];
        float Radius; // the glow sprite's half size (m)
        float Alpha;  // the sprite's alpha argument (1 for the plain glow)
    };
    FFarSphereNote GFarSpheres[kMaxFarSpheres];
    int GFarSphereCount = 0;
    FStarGlowNote GStarGlows[kMaxStarGlows];
    int GStarGlowCount = 0;
    double GFrameCamPos[3] = {0.0, 0.0, 0.0};
    // The extended asteroid walk's fade (CliAsteroidTickOne): the view depth where the cubes'
    // alpha reaches 0, the band width, the cube size; 0 = none this frame.
    float GAsteroidFadeEnd = 0.0f;
    float GAsteroidFadeBand = 0.0f;
    float GAsteroidCubeSize = 0.0f;

    // One lightning strike: a point light inside a nebula's clouds with a flicker envelope of
    // 2..4 sub-pulses (each a fast rise and an exponential decay, later ones weaker and
    // displaced a little, like the return strokes of a real discharge). Fixed slots, no
    // allocation (UpdateLightning).
    struct FLightningStrike
    {
        FNebulaEffect* Nebula; // null = free slot
        double Pos[3];         // world position of the first pulse
        double Start;          // SecondsNow
        float Lifetime;        // s
        float Colour[3];       // hue (peak channel 1) x peak intensity: radiance at the core
        float Peak;            // the peak intensity alone
        float Cloud;           // base cloud amount (0..1) at the strike
        float Radius;          // light radius (m)
        int PulseCount;
        float PulseStart[kMaxStrikePulses];
        float PulseAmp[kMaxStrikePulses];
        float PulseTau[kMaxStrikePulses];
        float PulseOffset[kMaxStrikePulses][3];
        float Envelope;        // this frame, flicker included (0..1.25)
        int Pulse;             // the strongest pulse this frame (its offset is used)
        // Ship light settings of the strike's nebula.
        int LightsShips;
        float ShipIntensity;
        float ShipDistance;
    };

    FLightningStrike GStrikes[kMaxStrikes];
    // The volume's own ship light (lightning_lights_ships 2, for nebulae without a
    // [DynamicLightning] light); created on first use, two references of our own.
    FGameLight* GShipLight = nullptr;
    bool GbShipLightFailed = false;
    // The nebula whose [DynamicLightning] GameLight (+0x270) the strikes drive this frame
    // (NebulaAmbientTick leaves it alone: DrivesNebulaLight).
    FNebulaEffect* GDrivenNebula = nullptr;
    // The lightning's own random numbers (xorshift32): retail's rand() sequence stays as it is.
    unsigned int GRandomState = 0;

    LARGE_INTEGER GTimeOrigin = {};
    LARGE_INTEGER GTimeFrequency = {};

    // Shader constants, filled in place every frame (no allocation).
    float GRaymarchConstants[kRaymarchRegisterCount * 4];
    float GCompositeConstants[kCompositeRegisterCount * 4];

    // ---- helpers --------------------------------------------------------------------------

    float Clamp(float V, float Lo, float Hi)
    {
        return V < Lo ? Lo : (V > Hi ? Hi : V);
    }

    double Frac(double V)
    {
        return V - std::floor(V);
    }

    // "solar\\nebula\\li01_badlands_nebula.ini" -> "li01_badlands_nebula".
    void BaseName(char* Out, size_t OutSize, const char* Path)
    {
        Out[0] = '\0';
        if (Path == nullptr)
            return;
        const char* Start = Path;
        for (const char* Cursor = Path; *Cursor != '\0'; ++Cursor)
        {
            if (*Cursor == '\\' || *Cursor == '/')
                Start = Cursor + 1;
        }
        size_t Length = 0;
        while (Start[Length] != '\0' && Start[Length] != '.' && Length + 1 < OutSize)
        {
            Out[Length] = Start[Length];
            ++Length;
        }
        Out[Length] = '\0';
    }

    bool ApplySettingKey(FNebulaVolumeSettings* Settings, INI_Reader& Reader)
    {
        const size_t Count = sizeof(kSettingKeys) / sizeof(kSettingKeys[0]);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_value(kSettingKeys[Index].Key))
            {
                Settings->*(kSettingKeys[Index].Member) = (float)(Reader.get_value_float(0));
                return true;
            }
        }
        const size_t VectorCount = sizeof(kSettingVectorKeys) / sizeof(kSettingVectorKeys[0]);
        for (size_t Index = 0; Index < VectorCount; ++Index)
        {
            if (Reader.is_value(kSettingVectorKeys[Index].Key))
            {
                float* const Values = Settings->*(kSettingVectorKeys[Index].Member);
                const unsigned int Given = Reader.get_num_parameters();
                for (unsigned int Component = 0; Component < 3; ++Component)
                    Values[Component] =
                        Component < Given ? (float)(Reader.get_value_float(Component)) : 0.0f;
                return true;
            }
        }
        return false;
    }

    FNebulaVolumeSettings const& SettingsFor(FNebulaEffect const* Nebula)
    {
        const char* File = nullptr;
        for (size_t Index = 0; Index < GNebulae.size(); ++Index)
        {
            if (GNebulae[Index].Nebula == Nebula)
            {
                File = GNebulae[Index].File;
                break;
            }
        }
        if (File != nullptr)
        {
            for (size_t Index = 0; Index < GOverrides.size(); ++Index)
            {
                if (_stricmp(GOverrides[Index].File, File) == 0)
                    return GOverrides[Index].Settings;
            }
        }
        return GSettings;
    }

    // ---- noise volume ---------------------------------------------------------------------

    unsigned int Hash(unsigned int X)
    {
        X ^= X >> 16;
        X *= 0x7feb352du;
        X ^= X >> 15;
        X *= 0x846ca68bu;
        X ^= X >> 16;
        return X;
    }

    unsigned int Hash3(int X, int Y, int Z, unsigned int Seed)
    {
        return Hash((unsigned int)(X) + Hash((unsigned int)(Y) + Hash((unsigned int)(Z) + Seed)));
    }

    int Wrap(int Value, int Period)
    {
        const int Mod = Value % Period;
        return Mod < 0 ? Mod + Period : Mod;
    }

    float Fade(float T)
    {
        return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
    }

    float Lerp(float A, float B, float T)
    {
        return A + (B - A) * T;
    }

    // Gradient of a lattice corner: one of the 12 cube edge directions.
    float GradientDot(unsigned int H, float X, float Y, float Z)
    {
        switch (H % 12u)
        {
        case 0: return X + Y;
        case 1: return -X + Y;
        case 2: return X - Y;
        case 3: return -X - Y;
        case 4: return X + Z;
        case 5: return -X + Z;
        case 6: return X - Z;
        case 7: return -X - Z;
        case 8: return Y + Z;
        case 9: return -Y + Z;
        case 10: return Y - Z;
        default: return -Y - Z;
        }
    }

    // Tileable gradient noise, period Period cells; P in cell units. About -1..1.
    float Perlin(float Px, float Py, float Pz, int Period, unsigned int Seed)
    {
        const int Ix = (int)(std::floor(Px));
        const int Iy = (int)(std::floor(Py));
        const int Iz = (int)(std::floor(Pz));
        const float Fx = Px - (float)(Ix);
        const float Fy = Py - (float)(Iy);
        const float Fz = Pz - (float)(Iz);
        float Corner[8];
        for (int Index = 0; Index < 8; ++Index)
        {
            const int Dx = Index & 1;
            const int Dy = (Index >> 1) & 1;
            const int Dz = (Index >> 2) & 1;
            const unsigned int H = Hash3(Wrap(Ix + Dx, Period), Wrap(Iy + Dy, Period),
                                         Wrap(Iz + Dz, Period), Seed);
            Corner[Index] = GradientDot(H, Fx - (float)(Dx), Fy - (float)(Dy), Fz - (float)(Dz));
        }
        const float U = Fade(Fx);
        const float V = Fade(Fy);
        const float W = Fade(Fz);
        const float X00 = Lerp(Corner[0], Corner[1], U);
        const float X10 = Lerp(Corner[2], Corner[3], U);
        const float X01 = Lerp(Corner[4], Corner[5], U);
        const float X11 = Lerp(Corner[6], Corner[7], U);
        return Lerp(Lerp(X00, X10, V), Lerp(X01, X11, V), W);
    }

    // One jittered feature point per cell (cell units, inside its cell) for a Worley octave.
    void BuildWorleyPoints(std::vector<float>* Points, int Period, unsigned int Seed)
    {
        Points->resize((size_t)(Period) * Period * Period * 3);
        for (int Z = 0; Z < Period; ++Z)
        {
            for (int Y = 0; Y < Period; ++Y)
            {
                for (int X = 0; X < Period; ++X)
                {
                    const unsigned int H = Hash3(X, Y, Z, Seed);
                    const size_t Base = (((size_t)(Z) * Period + Y) * Period + X) * 3;
                    (*Points)[Base + 0] = (float)(X) + (float)(H & 0x3ffu) / 1024.0f;
                    (*Points)[Base + 1] = (float)(Y) + (float)((H >> 10) & 0x3ffu) / 1024.0f;
                    (*Points)[Base + 2] = (float)(Z) + (float)((H >> 20) & 0x3ffu) / 1024.0f;
                }
            }
        }
    }

    // Inverted F1 (1 at a feature point, 0 a cell away or more), tileable.
    float WorleyInv(const std::vector<float>& Points, int Period, float Px, float Py, float Pz)
    {
        const int Ix = (int)(std::floor(Px));
        const int Iy = (int)(std::floor(Py));
        const int Iz = (int)(std::floor(Pz));
        float Best = 1.0e9f;
        for (int Dz = -1; Dz <= 1; ++Dz)
        {
            for (int Dy = -1; Dy <= 1; ++Dy)
            {
                for (int Dx = -1; Dx <= 1; ++Dx)
                {
                    const int Cx = Ix + Dx;
                    const int Cy = Iy + Dy;
                    const int Cz = Iz + Dz;
                    const size_t Base = (((size_t)(Wrap(Cz, Period)) * Period + Wrap(Cy, Period)) *
                                             Period +
                                         Wrap(Cx, Period)) *
                                        3;
                    // The stored point sits in the wrapped cell; move it to the unwrapped one.
                    const float Qx = Points[Base + 0] + (float)(Cx - Wrap(Cx, Period));
                    const float Qy = Points[Base + 1] + (float)(Cy - Wrap(Cy, Period));
                    const float Qz = Points[Base + 2] + (float)(Cz - Wrap(Cz, Period));
                    const float Ex = Qx - Px;
                    const float Ey = Qy - Py;
                    const float Ez = Qz - Pz;
                    const float DistSq = Ex * Ex + Ey * Ey + Ez * Ez;
                    if (DistSq < Best)
                        Best = DistSq;
                }
            }
        }
        return 1.0f - Clamp(std::sqrt(Best), 0.0f, 1.0f);
    }

    // Fills Texels (kNoiseSize^3 dwords, 0xAARRGGBB) and BaseHistogram (256 counts of the R
    // bytes). Deterministic. Each channel is stretched to the full 0..255 range.
    void GenerateNoise(std::vector<unsigned long>* Texels, unsigned long* BaseHistogram)
    {
        const int Size = (int)(kNoiseSize);
        const size_t Count = (size_t)(Size) * Size * Size;
        std::vector<float> Channels(Count * 4);

        std::vector<float> Worley4;
        std::vector<float> Worley8;
        std::vector<float> Worley16;
        std::vector<float> Worley32;
        BuildWorleyPoints(&Worley4, 4, 0x1001u);
        BuildWorleyPoints(&Worley8, 8, 0x2002u);
        BuildWorleyPoints(&Worley16, 16, 0x3003u);
        BuildWorleyPoints(&Worley32, 32, 0x4004u);

        float Min[4] = {1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f};
        float Max[4] = {-1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f};
        for (int Z = 0; Z < Size; ++Z)
        {
            for (int Y = 0; Y < Size; ++Y)
            {
                for (int X = 0; X < Size; ++X)
                {
                    // Tile coordinates 0..1 at texel centres.
                    const float Tx = ((float)(X) + 0.5f) / (float)(Size);
                    const float Ty = ((float)(Y) + 0.5f) / (float)(Size);
                    const float Tz = ((float)(Z) + 0.5f) / (float)(Size);

                    const float Perlin4 = Perlin(Tx * 4.0f, Ty * 4.0f, Tz * 4.0f, 4, 0x11u);
                    const float Perlin8 = Perlin(Tx * 8.0f, Ty * 8.0f, Tz * 8.0f, 8, 0x22u);
                    const float Perlin16 = Perlin(Tx * 16.0f, Ty * 16.0f, Tz * 16.0f, 16, 0x33u);
                    const float W4 = WorleyInv(Worley4, 4, Tx * 4.0f, Ty * 4.0f, Tz * 4.0f);
                    const float W8 = WorleyInv(Worley8, 8, Tx * 8.0f, Ty * 8.0f, Tz * 8.0f);
                    const float W16 = WorleyInv(Worley16, 16, Tx * 16.0f, Ty * 16.0f, Tz * 16.0f);
                    const float W32 = WorleyInv(Worley32, 32, Tx * 32.0f, Ty * 32.0f, Tz * 32.0f);

                    // R: Perlin fBm remapped by low-frequency Worley -- billowy cloud shapes.
                    const float PerlinFbm =
                        0.5f + 0.5f * ((Perlin4 + 0.5f * Perlin8 + 0.25f * Perlin16) / 1.75f) * 1.4f;
                    const float WorleyFbm = 0.625f * W4 + 0.25f * W8 + 0.125f * W16;
                    const float Billow = (PerlinFbm - (WorleyFbm - 1.0f)) / (2.0f - WorleyFbm);
                    // G / B: higher Worley octaves (detail / erosion).
                    const float Detail8 = 0.625f * W8 + 0.25f * W16 + 0.125f * W32;
                    const float Detail16 = 0.625f * W16 + 0.375f * W32;
                    // A: independent low-frequency Perlin for the colour blend.
                    const float Colour = 0.5f + 0.5f * (Perlin(Tx * 4.0f, Ty * 4.0f, Tz * 4.0f, 4,
                                                               0x55u) +
                                                        0.5f * Perlin(Tx * 8.0f, Ty * 8.0f,
                                                                      Tz * 8.0f, 8, 0x66u)) /
                                                           1.5f * 1.4f;

                    const size_t Index = ((size_t)(Z) * Size + Y) * Size + X;
                    const float Values[4] = {Billow, Detail8, Detail16, Colour};
                    for (int Channel = 0; Channel < 4; ++Channel)
                    {
                        Channels[Index * 4 + Channel] = Values[Channel];
                        if (Values[Channel] < Min[Channel])
                            Min[Channel] = Values[Channel];
                        if (Values[Channel] > Max[Channel])
                            Max[Channel] = Values[Channel];
                    }
                }
            }
        }

        for (int Bin = 0; Bin < 256; ++Bin)
            BaseHistogram[Bin] = 0;
        Texels->resize(Count);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            unsigned long Bytes[4];
            for (int Channel = 0; Channel < 4; ++Channel)
            {
                const float Range = Max[Channel] - Min[Channel];
                const float Norm =
                    Range > 1.0e-6f ? (Channels[Index * 4 + Channel] - Min[Channel]) / Range : 0.5f;
                Bytes[Channel] = (unsigned long)(Clamp(Norm, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            ++BaseHistogram[Bytes[0]];
            // A8R8G8B8: R channel 0, G 1, B 2, A 3.
            (*Texels)[Index] = (Bytes[3] << 24) | (Bytes[0] << 16) | (Bytes[1] << 8) | Bytes[2];
        }
    }

    // Coverage (the fraction of space inside clouds, before erosion) -> the base value where the
    // cloud starts (OutLow) and 1 / the width of its soft ramp: Softness of the cloud volume is
    // the ramp, the rest is full density.
    void CoverageRemap(float Coverage, float Softness, float* OutLow, float* OutInvWidth)
    {
        Coverage = Clamp(Coverage, 0.0f, 1.0f);
        Softness = Clamp(Softness, 0.02f, 1.0f);
        if (!(Coverage > 0.0f))
        {
            *OutLow = 2.0f; // above every noise value: no clouds
            *OutInvWidth = 1.0f;
            return;
        }
        float Low = 0.6f;
        float High = 0.8f;
        if (GbBaseHistogramValid)
        {
            const double Total = (double)(kNoiseSize) * (double)(kNoiseSize) * (double)(kNoiseSize);
            const double LowTarget = (double)(Coverage) * Total;
            const double HighTarget = (double)(Coverage) * (1.0 - (double)(Softness)) * Total;
            double Above = 0.0;
            int LowByte = 0;
            int HighByte = 255;
            bool bHighFound = false;
            for (int Byte = 255; Byte >= 0; --Byte)
            {
                Above += (double)(GBaseHistogram[Byte]);
                if (!bHighFound && Above >= HighTarget)
                {
                    HighByte = Byte;
                    bHighFound = true;
                }
                if (Above >= LowTarget)
                {
                    LowByte = Byte;
                    break;
                }
            }
            Low = (float)(LowByte) / 255.0f;
            High = (float)(HighByte) / 255.0f;
        }
        if (High < Low + 1.0f / 255.0f)
            High = Low + 1.0f / 255.0f;
        *OutLow = Low;
        *OutInvWidth = 1.0f / (High - Low);
    }

    // ---- resources ------------------------------------------------------------------------

    bool CreateVolumeTarget(IRenderPipelineEx* Ex, float Scale)
    {
        if (GVolumeTarget != nullptr)
        {
            Ex->DestroyExTexture(GVolumeTarget);
            GVolumeTarget = nullptr;
        }
        FRPExRenderTargetDesc Desc;
        Desc.Width = 0;
        Desc.Height = 0;
        Desc.BackBufferScale = Scale;
        Desc.Format = GbFloat16Target ? RPExFormatA16B16G16R16F : RPExFormatA8R8G8B8;
        GbVolumeTargetFloat16 = GbFloat16Target;
        if (Ex->CreateExRenderTarget(&Desc, &GVolumeTarget) != GR_OK && GbFloat16Target)
        {
            // FP16 refused: 8-bit RGBA; the ray march then tonemaps itself (display values and
            // transmittance both fit 0..1).
            Desc.Format = RPExFormatA8R8G8B8;
            GbVolumeTargetFloat16 = false;
            Ex->CreateExRenderTarget(&Desc, &GVolumeTarget);
        }
        GVolumeTargetScale = Scale;
        return GVolumeTarget != nullptr;
    }

    // Lazily, the first frame the volume could be drawn: programs (once; a reload recompiles
    // them behind the same handles), noise volume (once), render target (again when
    // resolution_scale changes). A step that failed is not retried every frame.
    bool EnsureResources(IRenderPipelineEx* Ex, FNebulaVolumeSettings const& Settings)
    {
        if (!GbProgramsCreated)
        {
            GbProgramsCreated = true;
            // A failed compile still returns a handle (RP9 logs the errors); it stays unusable
            // until a ReloadExPrograms compiles it.
            const GENRESULT RaymarchResult =
                Ex->CreateExProgram(RenderEx::GetExtraData(), kNebulaVolumeShader, "VSMain",
                                    "PSRaymarch", &GRaymarchProgram);
            const GENRESULT CompositeResult =
                Ex->CreateExProgram(RenderEx::GetExtraData(), kNebulaCompositeShader, "VSMain",
                                    "PSComposite",
                                    &GCompositeProgram);
            if (RaymarchResult != GR_OK || CompositeResult != GR_OK)
            {
                GbProgramFailed = true;
                FDUMP(0x100002, "%s(%d) : WARNING:General:nebula volume shaders did not compile "
                      "(errors above); retail interior fog until a shader reload",
                      kNebulaVolumeSource, __LINE__);
            }
        }
        if (GRaymarchProgram == nullptr || GCompositeProgram == nullptr || GbProgramFailed)
            return false;

        if (GNoiseVolume == nullptr)
        {
            if (GbNoiseFailed)
                return false;
            const DWORD Start = GetTickCount();
            GenerateNoise(&GNoiseTexels, GBaseHistogram);
            GbBaseHistogramValid = true;
            const GENRESULT VolumeResult =
                Ex->CreateExVolumeTexture(kNoiseSize, kNoiseSize, kNoiseSize, RPExFormatA8R8G8B8,
                                          &GNoiseTexels[0], &GNoiseVolume);
            FDUMP(0x100003, "%s(%d) : NOTICE:General:nebula noise volume %lu^3 generated in %lu ms",
                  kNebulaVolumeSource, __LINE__, kNoiseSize,
                  (unsigned long)(GetTickCount() - Start));
            if (VolumeResult != GR_OK || GNoiseVolume == nullptr)
            {
                GbNoiseFailed = true;
                GNoiseVolume = nullptr;
                FDUMP(0x100002, "%s(%d) : WARNING:General:nebula noise volume could not be "
                      "created", kNebulaVolumeSource, __LINE__);
                return false;
            }
        }

        const float Scale = Clamp(Settings.ResolutionScale, 0.125f, 1.0f);
        if (GVolumeTarget == nullptr || Scale != GVolumeTargetScale)
        {
            if (GbTargetFailed)
                return false;
            if (!CreateVolumeTarget(Ex, Scale))
            {
                GbTargetFailed = true;
                FDUMP(0x100002, "%s(%d) : WARNING:General:nebula volume render target could not "
                      "be created", kNebulaVolumeSource, __LINE__);
                return false;
            }
        }
        return true;
    }

    // Once per device lifetime of RenderEx's caps: shader model, slot budget, volume textures.
    // (The INTZ scene depth is checked every frame -- GetSceneDepth -- since a device reset may
    // change it.)
    bool QueryCaps()
    {
        if (GbCapsQueried)
            return GbCapsOk;
        FRPExCaps Caps;
        if (!RenderEx::GetCaps(&Caps))
            return false; // no device yet: ask again next frame
        GbCapsQueried = true;
        GbFloat16Target = Caps.bFloat16Target != 0;
        GExecutedLimit = Caps.MaxPixelShaderInstructionsExecuted != 0
                             ? (float)(Caps.MaxPixelShaderInstructionsExecuted)
                             : 65535.0f;
        if (GExecutedLimit > 16777216.0f)
            GExecutedLimit = 16777216.0f;
        const bool bShaderModel = Caps.PixelShaderVersion >= 0x300 &&
                                  Caps.VertexShaderVersion >= 0x300;
        const bool bSlots = Caps.MaxPixelShaderSlots >= kRaymarchPsSlotsRequired;
        GbCapsOk = bShaderModel && bSlots && Caps.bVolumeTextures != 0;
        if (GbCapsOk)
            FDUMP(0x100003, "%s(%d) : NOTICE:General:nebula volume available (ps slots %lu, "
                  "needs %lu; %s volume target)", kNebulaVolumeSource, __LINE__,
                  Caps.MaxPixelShaderSlots, kRaymarchPsSlotsRequired,
                  GbFloat16Target ? "FP16" : "A8R8G8B8");
        else if (bShaderModel && !bSlots)
            FDUMP(0x100002, "%s(%d) : WARNING:General:nebula volume disabled: the adapter allows "
                  "%lu pixel shader instruction slots, the ray march needs %lu",
                  kNebulaVolumeSource, __LINE__, Caps.MaxPixelShaderSlots,
                  kRaymarchPsSlotsRequired);
        else
            FDUMP(0x100002, "%s(%d) : WARNING:General:nebula volume disabled: needs vs/ps 3.0 and "
                  "volume textures (vs %lx, ps %lx, volume textures %d)", kNebulaVolumeSource,
                  __LINE__, Caps.VertexShaderVersion, Caps.PixelShaderVersion,
                  (int)(Caps.bVolumeTextures));
        return GbCapsOk;
    }

    // ---- per-frame parameters -------------------------------------------------------------

    // The zone's radii along its local axes (box, cylinder and ring: the inscribed ellipsoid).
    void ZoneRadii(Universe::IZone const* Zone, float* Radius)
    {
        const Vector Size = Zone->GetSize();
        switch (Zone->GetShape())
        {
        case Universe::ZoneShapeSphere:
            Radius[0] = Size.x;
            Radius[1] = Size.x;
            Radius[2] = Size.x;
            break;
        case Universe::ZoneShapeEllipsoid:
            Radius[0] = Size.x;
            Radius[1] = Size.y;
            Radius[2] = Size.z;
            break;
        case Universe::ZoneShapeBox:
            Radius[0] = Size.x * 0.5f;
            Radius[1] = Size.y * 0.5f;
            Radius[2] = Size.z * 0.5f;
            break;
        case Universe::ZoneShapeCylinder:
            Radius[0] = Size.x;
            Radius[1] = Size.y * 0.5f;
            Radius[2] = Size.x;
            break;
        case Universe::ZoneShapeRing:
            Radius[0] = Size.x;
            Radius[1] = Size.z * 0.5f;
            Radius[2] = Size.x;
            break;
        default:
        {
            const float R = Zone->GetRadius();
            Radius[0] = R;
            Radius[1] = R;
            Radius[2] = R;
            break;
        }
        }
    }

    // Rows that take a camera-relative point into the zone's unit space (|local| = 1 on the
    // surface): row j = the zone's local axis j (column j of its orientation) / radius j.
    void ZoneLocalRows(Universe::IZone const* Zone, float* Row0, float* Row1, float* Row2)
    {
        const Matrix& Orientation = Zone->GetOrientation();
        float Radius[3];
        ZoneRadii(Zone, Radius);
        float* const Rows[3] = {Row0, Row1, Row2};
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Inv = Radius[Axis] > 1.0e-3f ? 1.0f / Radius[Axis] : 0.0f;
            Rows[Axis][0] = Orientation.E[0][Axis] * Inv;
            Rows[Axis][1] = Orientation.E[1][Axis] * Inv;
            Rows[Axis][2] = Orientation.E[2][Axis] * Inv;
        }
    }

    void Set4(float* Register, float X, float Y, float Z, float W)
    {
        Register[0] = X;
        Register[1] = Y;
        Register[2] = Z;
        Register[3] = W;
    }

    float* RaymarchRegister(int Index)
    {
        return &GRaymarchConstants[Index * 4];
    }

    // The first [Clouds] block of the nebula (FNebulaClouds raw offsets, cli_nebula.cpp
    // LocalNebulaCloudsParseIni): puff_radius +0xa8, puff_colora +0x78, puff_colorb +0x84,
    // puff_drift +0xb0, puff_max_alpha +0xb4, near_fade_distance +0xe4 / +0xe8.
    struct FCloudParams
    {
        float PuffRadius;
        float ColourA[3];
        float ColourB[3];
        float Drift;
        float MaxAlpha;
        float NearFadeA;
        float NearFadeB;
    };

    void ReadCloudParams(FNebulaEffect const* Nebula, float const* FogRgb, FCloudParams* Out)
    {
        Out->PuffRadius = 100.0f;
        Out->Drift = 1.0f;
        Out->MaxAlpha = 0.5f;
        Out->NearFadeA = 0.0f;
        Out->NearFadeB = 0.0f;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Out->ColourA[Channel] = Clamp(FogRgb[Channel] * 2.0f, 0.0f, 1.0f);
            Out->ColourB[Channel] = Out->ColourA[Channel];
        }
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        if (Sentinel == nullptr || Sentinel->Next == Sentinel || Sentinel->Next->Clouds == nullptr)
            return;
        unsigned char const* const C = Sentinel->Next->Clouds->Raw;
        float Value = 0.0f;
        memcpy(&Value, C + 0xa8, sizeof(float));
        if (Value > 1.0f)
            Out->PuffRadius = Value;
        memcpy(&Out->Drift, C + 0xb0, sizeof(float));
        memcpy(&Out->MaxAlpha, C + 0xb4, sizeof(float));
        memcpy(&Out->NearFadeA, C + 0xe4, sizeof(float));
        memcpy(&Out->NearFadeB, C + 0xe8, sizeof(float));
        float ColourA[3];
        float ColourB[3];
        memcpy(ColourA, C + 0x78, sizeof(ColourA));
        memcpy(ColourB, C + 0x84, sizeof(ColourB));
        if (ColourA[0] + ColourA[1] + ColourA[2] > 0.0f)
        {
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                Out->ColourA[Channel] = Clamp(ColourA[Channel], 0.0f, 1.0f);
                Out->ColourB[Channel] = Clamp(ColourB[Channel], 0.0f, 1.0f);
            }
        }
    }

    double SecondsNow()
    {
        if (GTimeFrequency.QuadPart == 0)
        {
            QueryPerformanceFrequency(&GTimeFrequency);
            QueryPerformanceCounter(&GTimeOrigin);
        }
        LARGE_INTEGER Now;
        QueryPerformanceCounter(&Now);
        if (GTimeFrequency.QuadPart == 0)
            return 0.0;
        return (double)(Now.QuadPart - GTimeOrigin.QuadPart) / (double)(GTimeFrequency.QuadPart);
    }

    // The two extinctions and the march cap of a nebula with a settings set.
    struct FMediumParams
    {
        float FogNear;
        float FogDistance;
        float HazeSigma;  // 1/m
        float CloudSigma; // 1/m at cloud density 1
        float MarchCap;   // metres from the ray's zone entry (the camera when inside)
    };

    void ComputeMedium(FNebulaEffect const* Nebula, FNebulaVolumeSettings const& S,
                       FMediumParams* Out)
    {
        Out->FogNear = Nebula->FogNear > 0.0f ? Nebula->FogNear : 0.0f;
        Out->FogDistance = Nebula->FogDistance > 1.0f ? Nebula->FogDistance : 1.0f;
        const double Density = S.DensityScale > 0.0f ? (double)(S.DensityScale) : 0.0;
        // Haze: exp(-sigma * [Fog] distance) = haze_transmittance_at_fog_distance.
        const double THaze = (double)(Clamp(S.HazeTransmittanceAtFogDistance, 1.0e-4f, 0.9999f));
        Out->HazeSigma = (float)(-std::log(THaze) / (double)(Out->FogDistance) * Density);
        // Clouds: cloud_density = the optical depth of [Fog] distance metres of full cloud.
        Out->CloudSigma = (float)((S.CloudDensity > 0.0f ? (double)(S.CloudDensity) : 0.0) /
                                  (double)(Out->FogDistance) * Density);

        float MaxMarch = S.MaxMarchDistance;
        if (!(MaxMarch > 0.0f))
        {
            // Where the haze alone leaves march_end_transmittance, but at least
            // auto_march_fog_distances times the largest exclusion fog_far span (the haze is
            // thinner in there), and between 1 and 30 [Fog] distances.
            const double TEnd = (double)(Clamp(S.MarchEndTransmittance, 1.0e-4f, 0.9f));
            double Span = Out->HazeSigma > 1.0e-9f ? -std::log(TEnd) / (double)(Out->HazeSigma)
                                                   : 1.0e9;
            float MaxFogFar = 0.0f;
            Exclusion::NebulaExclusionZoneMgr const* const Mgr = Nebula->ExclusionMgr;
            const int ZoneCount = Mgr != nullptr ? FNebulaVolumeExclusionAccess::Count(Mgr) : 0;
            for (int Index = 0; Index < ZoneCount; ++Index)
            {
                Exclusion::NebulaExclusionZone const* const Excl =
                    FNebulaVolumeExclusionAccess::At(Mgr, Index);
                if (Excl != nullptr && Excl->FogFar > MaxFogFar)
                    MaxFogFar = Excl->FogFar;
            }
            const double ExclusionSpan =
                (double)(MaxFogFar - Out->FogNear) *
                (S.AutoMarchFogDistances > 0.5f ? (double)(S.AutoMarchFogDistances) : 0.5);
            if (ExclusionSpan > Span)
                Span = ExclusionSpan;
            const double MinSpan = (double)(Out->FogDistance);
            const double MaxSpan = (double)(Out->FogDistance) * 30.0;
            Span = Span < MinSpan ? MinSpan : (Span > MaxSpan ? MaxSpan : Span);
            MaxMarch = (float)((double)(Out->FogNear) + Span);
        }
        Out->MarchCap = MaxMarch;
    }

    // The camera's distance to the zone surface scaled by Scale (1 = the surface itself; the
    // drawn bound is larger, the shell's half-density surface smaller) along the line to its
    // centre, as retail measures the immersion (FNebulaEffect::Update 0x0050706b..: length -
    // GetRadiusAlong); 0 inside.
    double SurfaceDistance(Universe::IZone const* Zone, double const* CamPos, double Scale)
    {
        const Vector& Pos = Zone->GetPos();
        const double Dx = CamPos[0] - (double)(Pos.x);
        const double Dy = CamPos[1] - (double)(Pos.y);
        const double Dz = CamPos[2] - (double)(Pos.z);
        const double Length = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        if (!(Length > 1.0e-3))
            return 0.0;
        Vector Dir;
        Dir.x = (float)(Dx / Length);
        Dir.y = (float)(Dy / Length);
        Dir.z = (float)(Dz / Length);
        const double Outside = Length - Scale * (double)(Zone->GetRadiusAlong(Dir));
        return Outside > 0.0 ? Outside : 0.0;
    }

    float MaxZoneRadius(Universe::IZone const* Zone)
    {
        float Radius[3];
        ZoneRadii(Zone, Radius);
        float Max = Radius[0] > Radius[1] ? Radius[0] : Radius[1];
        return Radius[2] > Max ? Radius[2] : Max;
    }

    // Inside an exclusion zone retail pushes the fog end out to its fog_far
    // (FNebulaEffect::Update, 0x00507230..0x00507272); the same visibility means the haze
    // density times distance / span, times exclusion_density_scale.
    float ExclusionInsideScale(Exclusion::NebulaExclusionZone const* Excl,
                               FMediumParams const& Medium, FNebulaVolumeSettings const& S)
    {
        float InsideScale = 1.0f;
        if (Excl->FogFar > Medium.FogNear)
            InsideScale = Clamp(Medium.FogDistance / (Excl->FogFar - Medium.FogNear), 0.0f, 1.0f);
        return Clamp(InsideScale * S.ExclusionDensityScale, 0.0f, 1.0f);
    }

    // The haze length that leaves asteroid_spawn_transmittance, with the haze around the camera:
    // inside an exclusion zone (the nebula's current one, +0x24 / immersion +0x28) thinned as
    // the shader thins it.
    float AsteroidHazeReach(FNebulaEffect* Nebula, FNebulaVolumeSettings const& S,
                            FMediumParams const& Medium, bool bInside)
    {
        double Sigma = (double)(Medium.HazeSigma);
        if (bInside && Nebula->Field24 != 0)
        {
            Exclusion::NebulaExclusionZone const* const Excl =
                reinterpret_cast<Exclusion::NebulaExclusionZone const*>(
                    (uintptr_t)(Nebula->Field24));
            float Immersion = 0.0f;
            memcpy(&Immersion, &Nebula->Field28, sizeof(float));
            Immersion = Clamp(Immersion, 0.0f, 1.0f);
            const float Scale = ExclusionInsideScale(Excl, Medium, S);
            Sigma *= (double)(1.0f + (Scale - 1.0f) * Immersion);
        }
        const double T = (double)(Clamp(S.AsteroidSpawnTransmittance, 1.0e-4f, 0.9999f));
        if (!(Sigma > 1.0e-9))
            return 1.0e9f;
        // The clear radius ([Fog] near) holds no haze at the camera (smoothstep): about half of
        // it counts.
        return (float)(-std::log(T) / Sigma + 0.5 * (double)(Medium.FogNear));
    }

    // The three noise lookups' drift: direction and speed factor per lookup (base, detail,
    // warp), times puff_drift * drift_scale metres per second.
    const double kDriftDir[3][3] = {{0.80, 0.20, 0.56}, {-0.57, 0.35, 0.74}, {0.29, -0.91, 0.30}};
    const double kDriftMul[3] = {1.0, 1.7, 2.6};

    // Where the cloud field of a nebula sits this frame: the scales, the noise space (the zone's
    // up axis compressed by the layer stretch), the drift, the coverage cut. Shared by the
    // shader constants (BuildRaymarchConstants) and the CPU evaluation of the same base shape
    // (WorldBaseCloud, the lightning placement), so both see the same clouds.
    struct FNoiseFrame
    {
        double Up[3];         // the zone's up axis (its local y), unit
        double Stretch;       // layer_stretch
        double CloudScale;    // the base cell (m)
        double DetailCell;    // the erosion cell (m)
        double Tiles[3];      // base, detail, warp tile (m)
        double DriftSpeed;    // m/s
        double Time;          // SecondsNow
        double WarpAmplitude; // m
        float RemapLow;       // coverage threshold of the base value
        float RemapInvWidth;  // 1 / its ramp width
    };

    void ComputeNoiseFrame(FNebulaEffect const* Nebula, FNebulaVolumeSettings const& S,
                           FMediumParams const& Medium, FCloudParams const& Clouds, double Time,
                           FNoiseFrame* Out)
    {
        Universe::IZone const* const Zone = Nebula->Zone;
        float ZoneRadius[3];
        ZoneRadii(Zone, ZoneRadius);
        float MinZoneRadius = ZoneRadius[0] < ZoneRadius[1] ? ZoneRadius[0] : ZoneRadius[1];
        MinZoneRadius = ZoneRadius[2] < MinZoneRadius ? ZoneRadius[2] : MinZoneRadius;

        // Base cell: cloud_scale_m, or cloud_scale_fog_distances x [Fog] distance kept below a
        // quarter of the zone's smallest radius; detail cell: puff_radius x detail_scale; the
        // warp noise runs at half the base cell.
        Out->DetailCell =
            (double)(Clouds.PuffRadius) * (S.DetailScale > 0.05f ? (double)(S.DetailScale) : 0.05);
        double CloudScale = (double)(S.CloudScaleM);
        if (!(CloudScale > 0.0))
        {
            CloudScale = (double)(Medium.FogDistance) *
                         (S.CloudScaleFogDistances > 0.05f ? (double)(S.CloudScaleFogDistances) : 0.05);
            if (MinZoneRadius > 1.0f && CloudScale > 0.25 * (double)(MinZoneRadius))
                CloudScale = 0.25 * (double)(MinZoneRadius);
            if (CloudScale < 2.0 * Out->DetailCell)
                CloudScale = 2.0 * Out->DetailCell;
        }
        if (CloudScale < 10.0)
            CloudScale = 10.0;
        Out->CloudScale = CloudScale;
        Out->Tiles[0] = CloudScale * kBaseCellsPerTile;
        Out->Tiles[1] = Out->DetailCell * kDetailCellsPerTile;
        Out->Tiles[2] = Out->Tiles[0] * 0.5;
        Out->Stretch = S.LayerStretch > 0.05f ? (double)(S.LayerStretch) : 0.05;

        const Matrix& Orientation = Zone->GetOrientation();
        Out->Up[0] = Orientation.E[0][1];
        Out->Up[1] = Orientation.E[1][1];
        Out->Up[2] = Orientation.E[2][1];
        const double UpLength =
            std::sqrt(Out->Up[0] * Out->Up[0] + Out->Up[1] * Out->Up[1] + Out->Up[2] * Out->Up[2]);
        if (UpLength > 1.0e-6)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
                Out->Up[Axis] /= UpLength;
        }
        else
        {
            Out->Up[0] = 0.0;
            Out->Up[1] = 1.0;
            Out->Up[2] = 0.0;
        }

        Out->Time = Time;
        Out->DriftSpeed = (double)(Clouds.Drift) * (double)(S.DriftScale);
        Out->WarpAmplitude = CloudScale * (S.Warp > 0.0f ? (double)(S.Warp) : 0.0);
        Out->RemapLow = 0.6f;
        Out->RemapInvWidth = 5.0f;
        CoverageRemap(S.Coverage, S.CloudSoftness, &Out->RemapLow, &Out->RemapInvWidth);
    }

    // The tile coordinate (0..1, wrapped) of lookup Lookup (0 base, 1 detail, 2 warp) for an
    // absolute noise-space coordinate: what the shader gets from the camera-relative point
    // times 1 / tile plus the c13..c15 offset.
    double NoiseTileCoordinate(FNoiseFrame const& Frame, int Lookup, int Axis, double NoiseAbs)
    {
        const double Moved =
            NoiseAbs - kDriftDir[Lookup][Axis] * Frame.DriftSpeed * kDriftMul[Lookup] * Frame.Time;
        return Frac(Moved / Frame.Tiles[Lookup] + 0.37 * (double)(Lookup));
    }

    // The noise volume sampled like the GPU does (linear, wrap, texel centres at (i + 0.5) / 64):
    // OutRgba 0..1.
    void SampleNoiseVolume(double U, double V, double W, float* OutRgba)
    {
        OutRgba[0] = 0.5f;
        OutRgba[1] = 0.5f;
        OutRgba[2] = 0.5f;
        OutRgba[3] = 0.5f;
        const int Size = (int)(kNoiseSize);
        if (GNoiseTexels.size() != (size_t)(Size) * Size * Size)
            return;
        const double Coord[3] = {U * (double)(Size) - 0.5, V * (double)(Size) - 0.5,
                                 W * (double)(Size) - 0.5};
        int Base[3];
        double Fraction[3];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const double Floor = std::floor(Coord[Axis]);
            Base[Axis] = (int)(Floor);
            Fraction[Axis] = Coord[Axis] - Floor;
        }
        double Sum[4] = {0.0, 0.0, 0.0, 0.0};
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const int Dx = Corner & 1;
            const int Dy = (Corner >> 1) & 1;
            const int Dz = (Corner >> 2) & 1;
            const double Weight = (Dx != 0 ? Fraction[0] : 1.0 - Fraction[0]) *
                                  (Dy != 0 ? Fraction[1] : 1.0 - Fraction[1]) *
                                  (Dz != 0 ? Fraction[2] : 1.0 - Fraction[2]);
            const size_t Index = ((size_t)(Wrap(Base[2] + Dz, Size)) * Size +
                                  (size_t)(Wrap(Base[1] + Dy, Size))) * Size +
                                 (size_t)(Wrap(Base[0] + Dx, Size));
            const unsigned long Texel = GNoiseTexels[Index];
            // A8R8G8B8 (GenerateNoise): R bits 16, G 8, B 0, A 24.
            Sum[0] += Weight * (double)((Texel >> 16) & 0xffu);
            Sum[1] += Weight * (double)((Texel >> 8) & 0xffu);
            Sum[2] += Weight * (double)(Texel & 0xffu);
            Sum[3] += Weight * (double)((Texel >> 24) & 0xffu);
        }
        for (int Channel = 0; Channel < 4; ++Channel)
            OutRgba[Channel] = (float)(Sum[Channel] / 255.0);
    }

    // The shader's base-shape cloud amount (NebulaMedium up to the coverage cut: warp, base,
    // no erosion, no zone mask, no far-away softening) at an absolute world position. Rise: the
    // shell's coverage rise there (the threshold moves that fraction of the way to 1).
    float WorldBaseCloud(FNoiseFrame const& Frame, double const* World, float Rise)
    {
        const double Along = World[0] * Frame.Up[0] + World[1] * Frame.Up[1] + World[2] * Frame.Up[2];
        double Noise[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Noise[Axis] = World[Axis] + Frame.Up[Axis] * Along * (Frame.Stretch - 1.0);
        float Warp[4];
        SampleNoiseVolume(NoiseTileCoordinate(Frame, 2, 0, Noise[0]),
                          NoiseTileCoordinate(Frame, 2, 1, Noise[1]),
                          NoiseTileCoordinate(Frame, 2, 2, Noise[2]), Warp);
        double Warped[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Warped[Axis] = Noise[Axis] + ((double)(Warp[Axis]) - 0.5) * Frame.WarpAmplitude;
        float Base[4];
        SampleNoiseVolume(NoiseTileCoordinate(Frame, 0, 0, Warped[0]),
                          NoiseTileCoordinate(Frame, 0, 1, Warped[1]),
                          NoiseTileCoordinate(Frame, 0, 2, Warped[2]), Base);
        const float Low = Frame.RemapLow + Rise * (1.0f - Frame.RemapLow);
        return Clamp((Base[0] - Low) * Frame.RemapInvWidth, 0.0f, 1.0f);
    }

    // The nebula's outer shell and halo in its unit space (nebula_volume.hlsl NebulaMedium,
    // c88..c90): the core, a shell of Width (radius fraction) over which the medium thins out
    // and the clouds get sparse, its radius displaced by low-frequency noise, and a thin haze
    // halo beyond the surface out to HaloEnd. Shared by the shader constants and the CPU
    // evaluation (lightning placement), so both see the same shape.
    struct FShellFrame
    {
        float Width;          // max(shell_width, edge_softness)
        float Amplitude;      // shell_noise: the radius displacement (+-, radius fraction)
        float NoiseStart;     // below this radius the displacement cannot reach the shell
        float CloudRise;      // shell_cloud_falloff: the coverage rise at the surface
        float HaloEnd;        // 1 + halo_extent (1 without a halo)
        float InvHalo;        // 1 / halo_extent
        float HaloDensity;    // halo_density (0 = no halo)
        float SpanScale;      // the drawn bound: HaloEnd + Amplitude
        float NoiseFrequency; // noise tiles per unit of normalised radius
        float NoiseOffset[3]; // per nebula, so two nebulae do not share one silhouette
    };

    void ComputeShellFrame(Universe::IZone const* Zone, FNebulaVolumeSettings const& S,
                           FShellFrame* Out)
    {
        // edge_softness stays the narrowest the shell can be (shell_width 0 = the edge ramp of
        // before, now eased).
        const float Edge = Clamp(S.EdgeSoftness, 0.001f, 1.0f);
        Out->Width = Clamp(S.ShellWidth > Edge ? S.ShellWidth : Edge, 0.001f, 1.0f);
        Out->Amplitude = Clamp(S.ShellNoise, 0.0f, 0.5f);
        // Without displacement the noise lookup is skipped everywhere.
        Out->NoiseStart = Out->Amplitude > 0.0f ? 1.0f - Out->Width - Out->Amplitude : 1.0e9f;
        Out->CloudRise = Clamp(S.ShellCloudFalloff, 0.0f, 4.0f);
        const float Extent = S.HaloExtent;
        const float Density = Clamp(S.HaloDensity, 0.0f, 1.0f);
        if (Extent > 0.001f && Density > 0.0f)
        {
            Out->HaloEnd = 1.0f + Extent;
            Out->InvHalo = 1.0f / Extent;
            Out->HaloDensity = Density;
        }
        else
        {
            Out->HaloEnd = 1.0f;
            Out->InvHalo = 1.0f;
            Out->HaloDensity = 0.0f;
        }
        Out->SpanScale = Out->HaloEnd + Out->Amplitude;
        // shell_noise_scale = the size of the lobes as a fraction of the radius: the alpha
        // channel's Perlin has 4 cells per tile (plus an octave of 8 at half weight).
        Out->NoiseFrequency = (float)(1.0 / (4.0 * (double)(S.ShellNoiseScale > 0.05f ? S.ShellNoiseScale
                                                                                     : 0.05f)));
        const Vector& Pos = Zone->GetPos();
        Out->NoiseOffset[0] = (float)(Frac((double)(Pos.x) * 1.37e-4 + 0.13));
        Out->NoiseOffset[1] = (float)(Frac((double)(Pos.y) * 1.71e-4 + 0.47));
        Out->NoiseOffset[2] = (float)(Frac((double)(Pos.z) * 1.19e-4 + 0.79));
    }

    // The shell at a point of the zone's unit space (no exterior term: that only thins what is
    // drawn): the medium's density inside the zone (NebulaShellProfile) and OutOuter, 0 in the
    // core .. 1 at the (displaced) surface.
    double ShellProfileAt(FShellFrame const& Shell, double const* Local, double* OutOuter)
    {
        double Qd = std::sqrt(Local[0] * Local[0] + Local[1] * Local[1] + Local[2] * Local[2]);
        if (Qd > (double)(Shell.NoiseStart))
        {
            float Noise[4];
            SampleNoiseVolume(Local[0] * (double)(Shell.NoiseFrequency) + (double)(Shell.NoiseOffset[0]),
                              Local[1] * (double)(Shell.NoiseFrequency) + (double)(Shell.NoiseOffset[1]),
                              Local[2] * (double)(Shell.NoiseFrequency) + (double)(Shell.NoiseOffset[2]),
                              Noise);
            Qd += (double)(Shell.Amplitude) * (2.0 * (double)(Noise[3]) - 1.0);
        }
        double Outer = (Qd - 1.0) / (double)(Shell.Width) + 1.0;
        Outer = Outer < 0.0 ? 0.0 : (Outer > 1.0 ? 1.0 : Outer);
        *OutOuter = Outer;
        return 1.0 - Outer * Outer;
    }

    // ---- lightning: shared helpers --------------------------------------------------------

    // 0..1 (xorshift32, seeded from the performance counter on first use).
    float Random01()
    {
        if (GRandomState == 0)
        {
            LARGE_INTEGER Counter;
            QueryPerformanceCounter(&Counter);
            GRandomState = (unsigned int)(Counter.LowPart) | 1u;
        }
        GRandomState ^= GRandomState << 13;
        GRandomState ^= GRandomState >> 17;
        GRandomState ^= GRandomState << 5;
        return (float)((double)(GRandomState >> 8) * (1.0 / 16777216.0));
    }

    float RandomRange(float Lo, float Hi)
    {
        return Lo + (Hi - Lo) * Random01();
    }

    // A point in the unit ball, uniformly (rejection; the centre after 16 misses).
    void RandomInBall(double* Out)
    {
        for (int Attempt = 0; Attempt < 16; ++Attempt)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
                Out[Axis] = 2.0 * (double)(Random01()) - 1.0;
            if (Out[0] * Out[0] + Out[1] * Out[1] + Out[2] * Out[2] <= 1.0)
                return;
        }
        Out[0] = 0.0;
        Out[1] = 0.0;
        Out[2] = 0.0;
    }

    // A unit direction, uniformly over the sphere.
    void RandomDirection(double* Out)
    {
        const double Z = 2.0 * (double)(Random01()) - 1.0;
        const double Phi = 2.0 * kPi * (double)(Random01());
        const double Ring = std::sqrt(1.0 - Z * Z > 0.0 ? 1.0 - Z * Z : 0.0);
        Out[0] = Ring * std::cos(Phi);
        Out[1] = Ring * std::sin(Phi);
        Out[2] = Z;
    }

    // The light radius of a nebula's strikes: lightning_radius, or 1.5 base cloud cells (a
    // strike sits deep in a cloud mass; the light has to reach the mass's surface, about a cell
    // away, to be seen at all).
    double StrikeRadius(FNebulaVolumeSettings const& S, double CloudScale)
    {
        return S.LightningRadius > 0.0f ? (double)(S.LightningRadius) : 1.5 * CloudScale;
    }

    // Where a strike lights from this frame: its position plus the strongest pulse's offset.
    void StrikePosition(FLightningStrike const& Strike, double* Out)
    {
        const int Pulse = Strike.Pulse >= 0 && Strike.Pulse < Strike.PulseCount ? Strike.Pulse : 0;
        for (int Axis = 0; Axis < 3; ++Axis)
            Out[Axis] = Strike.Pos[Axis] + (double)(Strike.PulseOffset[Pulse][Axis]);
    }

    // c69..c76 and c84..c86 of one nebula: its live strikes, nearest first, at most
    // kMaxPointLights (with [Debug] lightning_show_positions also the dark moments of a strike,
    // their cores seen through everything); the shared light shape from the settings; and the
    // faint haze flash of every strike of the nebula (lightning_haze_flash, weaker with the
    // distance in march caps) added to the ambient (c23). Returns the light count.
    int FillLightConstants(FVolumeEntry const& Entry, double const* CamPos, double CloudScale,
                           float MarchCap)
    {
        FNebulaVolumeSettings const& S = Entry.Settings;
        if (!(S.LightningEnabled > 0.5f))
            return 0;
        int Order[kMaxStrikes];
        double OrderDistSq[kMaxStrikes];
        int Count = 0;
        float Flash[3] = {0.0f, 0.0f, 0.0f};
        const double Cap = MarchCap > 1.0f ? (double)(MarchCap) : 1.0;
        const float FlashScale = S.LightningHazeFlash > 0.0f ? S.LightningHazeFlash : 0.0f;
        for (int Index = 0; Index < kMaxStrikes; ++Index)
        {
            FLightningStrike const& Strike = GStrikes[Index];
            if (Strike.Nebula != Entry.Nebula)
                continue;
            double Pos[3];
            StrikePosition(Strike, Pos);
            const double Dx = Pos[0] - CamPos[0];
            const double Dy = Pos[1] - CamPos[1];
            const double Dz = Pos[2] - CamPos[2];
            const double DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
            const float FlashWeight =
                (float)((double)(Strike.Envelope * FlashScale) / (1.0 + DistSq / (Cap * Cap)));
            for (int Channel = 0; Channel < 3; ++Channel)
                Flash[Channel] += Strike.Colour[Channel] * FlashWeight;
            if (!(Strike.Envelope > 0.002f) && !GbLightningShowPositions)
                continue;
            int Slot = Count;
            while (Slot > 0 && OrderDistSq[Slot - 1] > DistSq)
            {
                Order[Slot] = Order[Slot - 1];
                OrderDistSq[Slot] = OrderDistSq[Slot - 1];
                --Slot;
            }
            Order[Slot] = Index;
            OrderDistSq[Slot] = DistSq;
            ++Count;
        }
        if (Count > kMaxPointLights)
            Count = kMaxPointLights;
        for (int Light = 0; Light < Count; ++Light)
        {
            FLightningStrike const& Strike = GStrikes[Order[Light]];
            double Pos[3];
            StrikePosition(Strike, Pos);
            const float Level = GbLightningShowPositions && Strike.Envelope < 0.3f ? 0.3f
                                                                                   : Strike.Envelope;
            Set4(RaymarchRegister(kRegLightPos + Light), (float)(Pos[0] - CamPos[0]),
                 (float)(Pos[1] - CamPos[1]), (float)(Pos[2] - CamPos[2]), Strike.Cloud);
            Set4(RaymarchRegister(kRegLightColour + Light), Strike.Colour[0] * Level,
                 Strike.Colour[1] * Level, Strike.Colour[2] * Level, (float)(OrderDistSq[Light]));
        }

        // The light shape, the same for every strike of the nebula: radius, soft core of the
        // inverse-square falloff (lightning_core_size of the radius), the phase g, the medium
        // between light and sample (lightning_shadow 0 none, 1 local estimate, 2 + a midpoint
        // cloud sample; lightning_shadow_density scales its optical depth in the diffusion
        // falloff 1 / (1 + tau)), the emissive core (lightning_core_glow_size of the radius,
        // lightning_core_emission), the haze scale.
        const double Radius = StrikeRadius(S, CloudScale);
        const double Core = (double)(Clamp(S.LightningCoreSize, 0.01f, 1.0f)) * Radius;
        const double Glow = (double)(S.LightningCoreGlowSize > 0.001f ? S.LightningCoreGlowSize : 0.001f) *
                            Radius;
        const float PhaseG = Clamp(S.LightningPhaseG, -0.9f, 0.9f);
        const int ShadowMode = (int)(Clamp(S.LightningShadow, 0.0f, 2.0f) + 0.5f);
        Set4(RaymarchRegister(kRegLightShape), (float)(Core * Core), (float)(1.0 / (Radius * Radius)),
             1.0f - PhaseG * PhaseG,
             ShadowMode >= 1 && S.LightningShadowDensity > 0.0f ? S.LightningShadowDensity : 0.0f);
        Set4(RaymarchRegister(kRegLightMore), (float)(1.0 / (Glow * Glow)),
             S.LightningHazeScale > 0.0f ? S.LightningHazeScale : 0.0f,
             S.LightningCoreEmission > 0.0f ? S.LightningCoreEmission : 0.0f,
             ShadowMode >= 2 ? 1.0f : 0.0f);
        Set4(RaymarchRegister(kRegLightExtra), GbLightningShowPositions ? 1.0f : 0.0f,
             (float)(Radius * Radius), 1.0f + PhaseG * PhaseG, 2.0f * PhaseG);

        float* const Ambient = RaymarchRegister(23);
        for (int Channel = 0; Channel < 3; ++Channel)
            Ambient[Channel] += Flash[Channel];
        return Count;
    }

    struct FExclusionPick
    {
        Exclusion::NebulaExclusionZone const* Zone;
        double Distance; // camera to the zone's bounding sphere surface
    };

    // Fills the raymarch constants (layout: nebula_volume.hlsl) of one nebula and the
    // composite's tonemap register. bIgnoreDepth: drawn before the opaque scene (the scene
    // depth is not looked at). Returns false when the nebula cannot be drawn.
    bool BuildRaymarchConstants(FVolumeEntry const& Entry, ICamera* Camera, float Near, float Far,
                                unsigned long FullWidth, unsigned long FullHeight,
                                unsigned long LowWidth, unsigned long LowHeight, bool bIgnoreDepth,
                                float* OutUpsampleThreshold, float* OutTonemap)
    {
        FNebulaEffect* const Nebula = Entry.Nebula;
        FNebulaVolumeSettings const& S = Entry.Settings;
        Universe::IZone const* const Zone = Nebula->Zone;
        if (Zone == nullptr || FullWidth == 0 || FullHeight == 0)
            return false;
        memset(GRaymarchConstants, 0, sizeof(GRaymarchConstants));

        // Camera: position, view rotation (world -> game view, column-vector convention:
        // v = R p + T), projection as Win3DSetPerspective / RP9 SetPerspective build it.
        float const* const CamPosF = Camera->GetPosition();
        const double CamPos[3] = {CamPosF[0], CamPosF[1], CamPosF[2]};
        float const* const View = Camera->GetViewTransform();
        const float FovDeg = Camera->GetFovYDeg();
        const float Aspect = Camera->GetAspect();
        const float Cot = (float)(1.0 / std::tan(kPi / 180.0 * (double)(FovDeg)));
        const float InvDepth = (float)(1.0 / ((double)(Far) - (double)(Near)));
        const float ScaleX = (float)((double)(Cot) / (double)(Aspect));
        const float ScaleY = Cot;
        const float ProjA = (float)((double)(Far) * (double)(InvDepth));
        const float ProjB = (float)(-(double)(Far) * (double)(Near) * (double)(InvDepth));
        if (!(ScaleX != 0.0f) || !(ScaleY != 0.0f))
            return false;

        // c0 ProjZ (A, B, C, D): depth = (A z + B) / (C z + D).
        Set4(RaymarchRegister(0), ProjA, ProjB, 1.0f, 0.0f);
        // c1 the camera's viewport in target UV (ViewRect: left, top, right, bottom).
        int const* const Vp = Camera->GetViewport();
        const float VpW = (float)(Vp[2] - Vp[0]);
        const float VpH = (float)(Vp[3] - Vp[1]);
        if (VpW > 0.0f && VpH > 0.0f)
            Set4(RaymarchRegister(1), (float)(Vp[0]) / (float)(FullWidth),
                 (float)(Vp[1]) / (float)(FullHeight), VpW / (float)(FullWidth),
                 VpH / (float)(FullHeight));
        else
            Set4(RaymarchRegister(1), 0.0f, 0.0f, 1.0f, 1.0f);
        // c2 1/ScaleX, 1/ScaleY, half a full-resolution texel (the depth texel the low-res
        // pixel represents, sampled at its centre -- the composite uses the same one).
        Set4(RaymarchRegister(2), 1.0f / ScaleX, 1.0f / ScaleY, 0.5f / (float)(FullWidth),
             0.5f / (float)(FullHeight));
        // c3..c5 view rotation rows.
        for (int Row = 0; Row < 3; ++Row)
            Set4(RaymarchRegister(3 + Row), View[Row * 3 + 0], View[Row * 3 + 1],
                 View[Row * 3 + 2], 0.0f);

        // c6..c9 the nebula zone (c6.w unused: the shell width in c88 took edge_softness's
        // place).
        const Vector& ZonePos = Zone->GetPos();
        Set4(RaymarchRegister(6), (float)((double)(ZonePos.x) - CamPos[0]),
             (float)((double)(ZonePos.y) - CamPos[1]), (float)((double)(ZonePos.z) - CamPos[2]),
             0.0f);
        ZoneLocalRows(Zone, RaymarchRegister(7), RaymarchRegister(8), RaymarchRegister(9));
        const Matrix& Orientation = Zone->GetOrientation();

        // c88..c90 the outer shell and the halo (ComputeShellFrame, shared with the lightning
        // placement); c91.x the exterior thinning of the shell: (exterior_shell_density - 1)
        // times how far outside the camera is -- 0 in the core (inside the shell's inner
        // radius, undisplaced), rising (smoothstep) to 1 at the drawn bound, so the inside look
        // does not change and nothing jumps while flying in.
        FShellFrame Shell;
        ComputeShellFrame(Zone, S, &Shell);
        Set4(RaymarchRegister(kRegShell), 1.0f / Shell.Width, Shell.Amplitude, Shell.NoiseStart,
             Shell.CloudRise);
        Set4(RaymarchRegister(kRegHalo), Shell.HaloEnd, Shell.InvHalo, Shell.HaloDensity,
             Shell.SpanScale * Shell.SpanScale);
        Set4(RaymarchRegister(kRegShellNoise), Shell.NoiseOffset[0], Shell.NoiseOffset[1],
             Shell.NoiseOffset[2], Shell.NoiseFrequency);
        double CamQSq = 0.0;
        for (int Row = 0; Row < 3; ++Row)
        {
            float const* const ZoneRowF = RaymarchRegister(7 + Row);
            const double L = -((double)(ZoneRowF[0]) * (double)(RaymarchRegister(6)[0]) +
                               (double)(ZoneRowF[1]) * (double)(RaymarchRegister(6)[1]) +
                               (double)(ZoneRowF[2]) * (double)(RaymarchRegister(6)[2]));
            CamQSq += L * L;
        }
        const double ShellInner = 1.0 - (double)(Shell.Width);
        double ExteriorWeight = (std::sqrt(CamQSq) - ShellInner) / ((double)(Shell.SpanScale) - ShellInner);
        ExteriorWeight = ExteriorWeight < 0.0 ? 0.0 : (ExteriorWeight > 1.0 ? 1.0 : ExteriorWeight);
        ExteriorWeight = ExteriorWeight * ExteriorWeight * (3.0 - 2.0 * ExteriorWeight);
        const float ExteriorShellTerm =
            (float)(((double)(Clamp(S.ExteriorShellDensity, 0.0f, 4.0f)) - 1.0) * ExteriorWeight);

        // Nebula values: [Fog] near / distance / colour (+0x29c / +0x298 / +0x2a0),
        // [NebulaLight] ambient / sun_burnthrough_intensity, [Clouds].
        float FogRgb[3];
        Nebula->CopyFogRgb(FogRgb);
        FMediumParams Medium;
        ComputeMedium(Nebula, S, &Medium);
        const float FogNear = Medium.FogNear;
        FCloudParams Clouds;
        ReadCloudParams(Nebula, FogRgb, &Clouds);

        // Exclusion zones: the kMaxExclusions nearest to the camera.
        FExclusionPick Picks[kMaxExclusions];
        int PickCount = 0;
        Exclusion::NebulaExclusionZoneMgr const* const Mgr = Nebula->ExclusionMgr;
        const int ZoneCount = Mgr != nullptr ? FNebulaVolumeExclusionAccess::Count(Mgr) : 0;
        for (int Index = 0; Index < ZoneCount; ++Index)
        {
            Exclusion::NebulaExclusionZone const* const Excl =
                FNebulaVolumeExclusionAccess::At(Mgr, Index);
            if (Excl == nullptr || Excl->ZonePtr == nullptr)
                continue;
            const Vector& Pos = Excl->ZonePtr->GetPos();
            const double Dx = (double)(Pos.x) - CamPos[0];
            const double Dy = (double)(Pos.y) - CamPos[1];
            const double Dz = (double)(Pos.z) - CamPos[2];
            const double Distance =
                std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - (double)(Excl->ZonePtr->GetRadius());
            int Slot = PickCount;
            if (PickCount == kMaxExclusions)
            {
                // Replace the farthest pick when this one is nearer.
                Slot = 0;
                for (int Pick = 1; Pick < PickCount; ++Pick)
                {
                    if (Picks[Pick].Distance > Picks[Slot].Distance)
                        Slot = Pick;
                }
                if (!(Distance < Picks[Slot].Distance))
                    continue;
            }
            else
                ++PickCount;
            Picks[Slot].Zone = Excl;
            Picks[Slot].Distance = Distance;
        }
        // Nearest first: a ray tests the zones up to the last one whose bounding sphere it
        // crosses (nebula_volume.hlsl), so the near ones should come first.
        for (int Pick = 1; Pick < PickCount; ++Pick)
        {
            const FExclusionPick Moved = Picks[Pick];
            int Hole = Pick;
            while (Hole > 0 && Picks[Hole - 1].Distance > Moved.Distance)
            {
                Picks[Hole] = Picks[Hole - 1];
                --Hole;
            }
            Picks[Hole] = Moved;
        }
        for (int Pick = 0; Pick < PickCount; ++Pick)
        {
            Exclusion::NebulaExclusionZone const* const Excl = Picks[Pick].Zone;
            Universe::IZone const* const ExclZone = Excl->ZonePtr;
            const Vector& Pos = ExclZone->GetPos();
            const float InsideScale = ExclusionInsideScale(Excl, Medium, S);
            // Centre + 1 / edge fraction, row 0 + inside haze density scale - 1, row 1 +
            // bounding radius, row 2 (one array per register kind, indexed by the zone; the
            // reciprocal and the offset precomputed, see c18.w).
            Set4(RaymarchRegister(kRegExclusionCentre + Pick), (float)((double)(Pos.x) - CamPos[0]),
                 (float)((double)(Pos.y) - CamPos[1]), (float)((double)(Pos.z) - CamPos[2]),
                 1.0f / Clamp(ExclZone->GetEdgeFraction(), 0.001f, 1.0f));
            ZoneLocalRows(ExclZone, RaymarchRegister(kRegExclusionRow0 + Pick),
                          RaymarchRegister(kRegExclusionRow1 + Pick),
                          RaymarchRegister(kRegExclusionRow2 + Pick));
            RaymarchRegister(kRegExclusionRow0 + Pick)[3] = InsideScale - 1.0f;
            // The ellipsoid the shader tests lies inside the sphere of its largest radius
            // (a hair larger, against float rounding of the span test).
            RaymarchRegister(kRegExclusionRow1 + Pick)[3] = MaxZoneRadius(ExclZone) * 1.001f + 1.0f;
        }

        // Debug views: 3 haze only, 4 clouds only (the other two are drawn by the shader).
        float HazeSigma = Medium.HazeSigma;
        float CloudSigma = Medium.CloudSigma;
        if (GNebulaView == 3)
            CloudSigma = 0.0f;
        else if (GNebulaView == 4)
            HazeSigma = 0.0f;
        const bool bShaderView = GNebulaView == 1 || GNebulaView == 2;

        // c10 march: steps, distance cap, step distribution, jitter.
        Set4(RaymarchRegister(10), Clamp(S.StepCount, 1.0f, (float)(kMaxSteps)), Medium.MarchCap,
             Clamp(S.StepDistribution, 0.25f, 4.0f), Clamp(S.Jitter, 0.0f, 1.0f));
        // c11 medium: the two extinctions, the clear radius ([Fog] near), the debug view.
        Set4(RaymarchRegister(11), HazeSigma, CloudSigma, FogNear,
             bShaderView ? (float)(GNebulaView) : 0.0f);

        // Cloud scales, noise space, drift and coverage cut (ComputeNoiseFrame, shared with the
        // lightning placement).
        FNoiseFrame Frame;
        ComputeNoiseFrame(Nebula, S, Medium, Clouds, SecondsNow(), &Frame);
        const double DetailCell = Frame.DetailCell;
        const double CloudScale = Frame.CloudScale;
        const double DetailTile = Frame.Tiles[1];
        const double Stretch = Frame.Stretch;
        const double Up[3] = {Frame.Up[0], Frame.Up[1], Frame.Up[2]};

        // c12 noise space: the zone's up axis (its local y), layer stretch - 1.
        Set4(RaymarchRegister(12), (float)(Up[0]), (float)(Up[1]), (float)(Up[2]),
             (float)(Stretch - 1.0));

        // c13..c15 tile offsets of the base, detail and warp lookups: the camera position in
        // noise space and the drift (puff_drift * drift_scale metres per second, a different
        // direction and speed per lookup, so the clouds also change shape), wrapped to the
        // tile in double so the shader only sees small numbers.
        const double CamUp = CamPos[0] * Up[0] + CamPos[1] * Up[1] + CamPos[2] * Up[2];
        double CamNoise[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            CamNoise[Axis] = CamPos[Axis] + Up[Axis] * CamUp * (Stretch - 1.0);
        for (int Lookup = 0; Lookup < 3; ++Lookup)
        {
            float* const Off = RaymarchRegister(13 + Lookup);
            for (int Axis = 0; Axis < 3; ++Axis)
                Off[Axis] = (float)(NoiseTileCoordinate(Frame, Lookup, Axis, CamNoise[Axis]));
            Off[3] = (float)(1.0 / Frame.Tiles[Lookup]);
        }

        // c16 shape: coverage threshold and ramp (from the noise histogram), erosion, wispiness.
        Set4(RaymarchRegister(16), Frame.RemapLow, Frame.RemapInvWidth,
             Clamp(S.Erosion, 0.0f, 0.95f), Clamp(S.Wispiness, 0.0f, 1.0f));
        // c91 the exterior shell term (above), far_cloud_softening (the coverage ramp's width
        // grows by 1 + this at full LOD, about its middle), far_detail_fade (the edge detail
        // left at full LOD: 1 - this), the middle of the coverage ramp (no clouds: far above
        // every noise value, so no softening can bring them back).
        Set4(RaymarchRegister(kRegShellMore), ExteriorShellTerm,
             S.FarCloudSoftening > 0.0f ? S.FarCloudSoftening : 0.0f,
             Clamp(S.FarDetailFade, 0.0f, 1.0f),
             Frame.RemapLow < 1.5f ? Frame.RemapLow + 0.5f / Frame.RemapInvWidth : 1000.0f);
        // c17 warp amplitude, cloud near fade range ([Clouds] near_fade_distance x
        // cloud_near_fade_scale), haze tail.
        const float NearFadeScale = S.CloudNearFadeScale > 0.0f ? S.CloudNearFadeScale : 0.0f;
        const float FadeA = Clouds.NearFadeA * NearFadeScale;
        float FadeB = (Clouds.NearFadeB > Clouds.NearFadeA ? Clouds.NearFadeB : Clouds.NearFadeA + 1.0f) *
                      NearFadeScale;
        Set4(RaymarchRegister(17), (float)(Frame.WarpAmplitude), FadeA, FadeB,
             S.HazeTail > 0.5f ? 1.0f : 0.0f);
        const float FadeWidth = FadeB - FadeA > 1.0f ? FadeB - FadeA : 1.0f;

        // c18..c23 colours. Haze albedo: the [Fog] colour's hue at full brightness times
        // haze_albedo; self-glow of haze and clouds: the [Fog] colour times haze_emission /
        // emission; cloud albedo: the puff colours; ambient: [NebulaLight] ambient.
        float FogPeak = FogRgb[0] > FogRgb[1] ? FogRgb[0] : FogRgb[1];
        FogPeak = FogRgb[2] > FogPeak ? FogRgb[2] : FogPeak;
        const float HazeAlbedoScale = FogPeak > 1.0e-4f ? S.HazeAlbedo / FogPeak : 0.0f;
        // c19.w, c22.w, c87, c28.w, c91.w and c92 carry loop invariants the shader would
        // otherwise hold in temp registers: 1 / [Fog] near (c11.z; 0 = no clear radius), 1 /
        // the cloud near fade width (c17.z - c17.y), puff colour b - a, ms_extinction^2, the
        // coverage ramp's middle, the key direction in zone space.
        Set4(RaymarchRegister(18), FogRgb[0] * HazeAlbedoScale, FogRgb[1] * HazeAlbedoScale,
             FogRgb[2] * HazeAlbedoScale, 0.0f);
        Set4(RaymarchRegister(19), FogRgb[0] * S.HazeEmission, FogRgb[1] * S.HazeEmission,
             FogRgb[2] * S.HazeEmission, FogNear > 0.0f ? 1.0f / FogNear : 0.0f);
        const float PowderLength = (float)(DetailCell);
        Set4(RaymarchRegister(20), FogRgb[0] * S.Emission, FogRgb[1] * S.Emission,
             FogRgb[2] * S.Emission, Clamp(S.PowderStrength, 0.0f, 1.0f));
        Set4(RaymarchRegister(21), Clouds.ColourA[0] * S.CloudBrightness,
             Clouds.ColourA[1] * S.CloudBrightness, Clouds.ColourA[2] * S.CloudBrightness,
             CloudSigma * 2.0f * PowderLength);
        Set4(RaymarchRegister(22), Clouds.ColourB[0] * S.CloudBrightness,
             Clouds.ColourB[1] * S.CloudBrightness, Clouds.ColourB[2] * S.CloudBrightness,
             1.0f / FadeWidth);
        Set4(RaymarchRegister(kRegPuffColourDelta),
             RaymarchRegister(22)[0] - RaymarchRegister(21)[0],
             RaymarchRegister(22)[1] - RaymarchRegister(21)[1],
             RaymarchRegister(22)[2] - RaymarchRegister(21)[2], 0.0f);
        // Occlusion probe: up the zone's axis by ao_distance (0 = half the clouds' vertical
        // cell, base cell / layer stretch); in noise space that is Up * distance * stretch.
        const double AoDistance =
            S.AoDistance > 0.0f ? (double)(S.AoDistance) : 0.5 * CloudScale / Stretch;
        const float AoFactor =
            (float)((double)(CloudSigma) * AoDistance * (S.AoStrength > 0.0f ? (double)(S.AoStrength) : 0.0));
        unsigned char const* const Ambient = Nebula->GetAmbientBytes();
        if (Ambient != nullptr)
            Set4(RaymarchRegister(23), Ambient[0] / 255.0f * S.AmbientScale,
                 Ambient[1] / 255.0f * S.AmbientScale, Ambient[2] / 255.0f * S.AmbientScale,
                 AoFactor);
        else
            RaymarchRegister(23)[3] = AoFactor;
        Set4(RaymarchRegister(24), (float)(Up[0] * AoDistance * Stretch),
             (float)(Up[1] * AoDistance * Stretch), (float)(Up[2] * AoDistance * Stretch), 0.0f);

        // c25..c27 key light. key_light_source 0: the first star of the system (StarSphereList,
        // the burn-through source) when the nebula has sun_burnthrough_intensity > 0, tinted by
        // its star_glow inner colour, times sun_burnthrough_intensity * key_light_intensity;
        // otherwise (or with 1) the fixed key: key_light_dir in zone space, key_light_colour (0 =
        // [Clouds] puff_colora brightened) times key_light_fixed_intensity.
        double KeyDir[3] = {0.0, 1.0, 0.0};
        float KeyColour[3] = {0.0f, 0.0f, 0.0f};
        bool bStarKey = false;
        const float Burn = Nebula->GetFloat2C4();
        FStarSphereList* const Stars = StarSphereList();
        if (S.KeyLightSource < 0.5f && Stars != nullptr && Stars->Sentinel != nullptr &&
            Stars->Sentinel->Next != Stars->Sentinel && Stars->Sentinel->Next->Sphere != nullptr &&
            Burn > 0.0f)
        {
            FStarSphere const* const Star = Stars->Sentinel->Next->Sphere;
            Vector StarPos;
            StarSphereGetOwnerPosition(Star, &StarPos);
            // Toward the star from the camera inside; from outside the reference slides toward
            // the zone centre with the distance (s / (s + largest radius)), so a far nebula is
            // lit from its own position and nothing jumps at the surface.
            const double RefWeight =
                (double)(Entry.SurfaceDistance) /
                ((double)(Entry.SurfaceDistance) + (double)(MaxZoneRadius(Zone)) + 1.0);
            const double RefPos[3] = {CamPos[0] + ((double)(ZonePos.x) - CamPos[0]) * RefWeight,
                                      CamPos[1] + ((double)(ZonePos.y) - CamPos[1]) * RefWeight,
                                      CamPos[2] + ((double)(ZonePos.z) - CamPos[2]) * RefWeight};
            const double Sx = (double)(StarPos.x) - RefPos[0];
            const double Sy = (double)(StarPos.y) - RefPos[1];
            const double Sz = (double)(StarPos.z) - RefPos[2];
            const double Length = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
            if (Length > 1.0)
            {
                bStarKey = true;
                KeyDir[0] = Sx / Length;
                KeyDir[1] = Sy / Length;
                KeyDir[2] = Sz / Length;
                float Tint[3] = {1.0f, 1.0f, 1.0f};
                if (Star->StarGlow != nullptr)
                {
                    const float* const Inner = Star->StarGlow->InnerColor;
                    float Peak = Inner[0] > Inner[1] ? Inner[0] : Inner[1];
                    Peak = Inner[2] > Peak ? Inner[2] : Peak;
                    if (Peak > 0.0f)
                    {
                        // Half toward white: the glow colour is a hue, not a light level.
                        for (int Channel = 0; Channel < 3; ++Channel)
                            Tint[Channel] = 0.5f + 0.5f * Inner[Channel] / Peak;
                    }
                }
                const float Intensity = Burn * (S.KeyLightIntensity > 0.0f ? S.KeyLightIntensity : 0.0f);
                for (int Channel = 0; Channel < 3; ++Channel)
                    KeyColour[Channel] = Tint[Channel] * Intensity;
            }
        }
        if (!bStarKey)
        {
            double Local[3] = {S.KeyLightDir[0], S.KeyLightDir[1], S.KeyLightDir[2]};
            const double LocalLength =
                std::sqrt(Local[0] * Local[0] + Local[1] * Local[1] + Local[2] * Local[2]);
            if (LocalLength > 1.0e-6)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                    Local[Axis] /= LocalLength;
            }
            else
            {
                Local[0] = 0.0;
                Local[1] = 1.0;
                Local[2] = 0.0;
            }
            double World[3];
            for (int Row = 0; Row < 3; ++Row)
                World[Row] = Orientation.E[Row][0] * Local[0] + Orientation.E[Row][1] * Local[1] +
                             Orientation.E[Row][2] * Local[2];
            const double WorldLength =
                std::sqrt(World[0] * World[0] + World[1] * World[1] + World[2] * World[2]);
            if (WorldLength > 1.0e-6)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                    KeyDir[Axis] = World[Axis] / WorldLength;
            }
            float Colour[3] = {S.KeyLightColour[0], S.KeyLightColour[1], S.KeyLightColour[2]};
            if (!(Colour[0] + Colour[1] + Colour[2] > 0.0f))
            {
                float Peak = Clouds.ColourA[0] > Clouds.ColourA[1] ? Clouds.ColourA[0] : Clouds.ColourA[1];
                Peak = Clouds.ColourA[2] > Peak ? Clouds.ColourA[2] : Peak;
                for (int Channel = 0; Channel < 3; ++Channel)
                    Colour[Channel] = Peak > 0.0f ? 0.5f + 0.5f * Clouds.ColourA[Channel] / Peak : 1.0f;
            }
            const float Intensity = S.KeyLightFixedIntensity > 0.0f ? S.KeyLightFixedIntensity : 0.0f;
            for (int Channel = 0; Channel < 3; ++Channel)
                KeyColour[Channel] = Colour[Channel] * Intensity;
        }
        const double KeyUp = KeyDir[0] * Up[0] + KeyDir[1] * Up[1] + KeyDir[2] * Up[2];
        Set4(RaymarchRegister(25), (float)(KeyDir[0]), (float)(KeyDir[1]), (float)(KeyDir[2]),
             Clamp(S.PhaseG, 0.0f, 0.95f));
        Set4(RaymarchRegister(26), KeyColour[0], KeyColour[1], KeyColour[2],
             Clamp(S.PhaseBackG, 0.0f, 0.95f));
        Set4(RaymarchRegister(27), (float)(KeyDir[0] + Up[0] * KeyUp * (Stretch - 1.0)),
             (float)(KeyDir[1] + Up[1] * KeyUp * (Stretch - 1.0)),
             (float)(KeyDir[2] + Up[2] * KeyUp * (Stretch - 1.0)),
             Clamp(S.PhaseBackWeight, 0.0f, 1.0f));
        // c28 multiple scattering: octave strength, extinction and phase factors.
        const float MsExtinction = Clamp(S.MsExtinction, 0.0f, 1.0f);
        Set4(RaymarchRegister(28), Clamp(S.MsStrength, 0.0f, 1.0f), MsExtinction,
             Clamp(S.MsPhase, 0.0f, 1.0f), MsExtinction * MsExtinction);
        // c92 the key direction in the zone's unit space (the rows c7..c9) and its squared
        // length: the large-scale haze shadow's exit toward the key.
        float* const KeyLocal = RaymarchRegister(kRegKeyLocal);
        for (int Row = 0; Row < 3; ++Row)
        {
            float const* const ZoneRowF = RaymarchRegister(7 + Row);
            KeyLocal[Row] = (float)((double)(ZoneRowF[0]) * KeyDir[0] + (double)(ZoneRowF[1]) * KeyDir[1] +
                                    (double)(ZoneRowF[2]) * KeyDir[2]);
        }
        const float KeyLocalSq =
            KeyLocal[0] * KeyLocal[0] + KeyLocal[1] * KeyLocal[1] + KeyLocal[2] * KeyLocal[2];
        KeyLocal[3] = KeyLocalSq > 1.0e-12f ? KeyLocalSq : 1.0e-12f;

        // c29..c34 shadow steps toward the key: shadow_steps segments growing by
        // kShadowStepGrowth over shadow_distance (0 = one base cell); each sample sits in its
        // segment's middle and carries sigma * shadow_density * the segment length.
        int ShadowSteps = (int)(Clamp(S.ShadowSteps, 0.0f, (float)(kMaxShadowSteps)) + 0.5f);
        const double ShadowDistance =
            S.ShadowDistance > 0.0f ? (double)(S.ShadowDistance) : CloudScale;
        const double GrowthSum = (std::pow(kShadowStepGrowth, (double)(ShadowSteps)) - 1.0) /
                                 (kShadowStepGrowth - 1.0);
        double Segment = ShadowSteps > 0 ? ShadowDistance / GrowthSum : 0.0;
        double Covered = 0.0;
        const double ShadowSigma =
            (double)(CloudSigma) * (S.ShadowDensity > 0.0f ? (double)(S.ShadowDensity) : 0.0);
        // c81..c83 the level-of-detail set: the same segments merged in pairs, each pair sampled
        // in its middle with the summed weight.
        double PairStart = 0.0;
        double PairLength = 0.0;
        double PairWeight = 0.0;
        for (int Step = 0; Step < kMaxShadowSteps; ++Step)
        {
            if (Step < ShadowSteps)
            {
                Set4(RaymarchRegister(kRegShadowStep + Step), (float)(Covered + 0.5 * Segment),
                     (float)(ShadowSigma * Segment), 0.0f, 0.0f);
                if ((Step & 1) == 0)
                    PairStart = Covered;
                PairLength += Segment;
                PairWeight += ShadowSigma * Segment;
                Covered += Segment;
                Segment *= kShadowStepGrowth;
            }
            if ((Step & 1) == 1 && Step / 2 < kMaxShadowStepsHalf)
            {
                if (PairWeight > 0.0)
                    Set4(RaymarchRegister(kRegShadowStepHalf + Step / 2),
                         (float)(PairStart + 0.5 * PairLength), (float)(PairWeight), 0.0f, 0.0f);
                PairLength = 0.0;
                PairWeight = 0.0;
            }
        }

        // c69..c76, c84..c86 the lightning point lights (none in debug views 1 and 2, which
        // accumulate in the light sum), the haze flash added to the ambient (c23).
        const int LightCount =
            bShaderView ? 0 : FillLightConstants(Entry, CamPos, CloudScale, Medium.MarchCap);

        // c35 exclusion count, point light count, in-shader tonemap (8-bit target only) and its
        // exposure.
        const float TonemapMode = Clamp(S.Tonemap, 0.0f, 2.0f);
        const float Exposure = S.Exposure > 0.0f ? S.Exposure : 0.0f;
        const bool bShaderTonemap = !GbVolumeTargetFloat16 && !bShaderView;
        Set4(RaymarchRegister(35), (float)(PickCount), (float)(LightCount),
             bShaderTonemap ? (TonemapMode > 0.5f ? TonemapMode : 0.0f) : 0.0f,
             bShaderTonemap ? Exposure : 1.0f);
        // c36 low-resolution target size (jitter pattern), Reinhard white.
        const float White = S.TonemapWhite > 0.01f ? S.TonemapWhite : 0.01f;
        Set4(RaymarchRegister(36), (float)(LowWidth), (float)(LowHeight), White, 0.0f);

        // c77 level of detail by the distance from the camera to the ray's zone entry (none
        // from inside: the entry is the camera): start, 1 / range (0 = off), the step fraction
        // reached at the end, the LOD where the shadow ray switches to the pair-merged set.
        const float LodRange = S.LodEndDistance - S.LodStartDistance;
        Set4(RaymarchRegister(kRegLod), S.LodStartDistance > 0.0f ? S.LodStartDistance : 0.0f,
             LodRange > 1.0f ? 1.0f / LodRange : 0.0f, Clamp(S.LodMinStepFraction, 0.05f, 1.0f),
             Clamp(S.LodShadowSwitch, 0.001f, 2.0f));
        // c78 exterior strength, the angle of a low-resolution pixel (the footprint of a sample
        // T metres away is T times it), 1 / the detail noise texel (lod_detail_fade; 0 = off),
        // ignore the scene depth.
        const float LowViewHeight = (float)(LowHeight) * RaymarchRegister(1)[3];
        const float PixelAngle = LowViewHeight > 0.5f ? 2.0f / (ScaleY * LowViewHeight) : 0.0f;
        const double DetailTexel = DetailTile / (double)(kNoiseSize);
        Set4(RaymarchRegister(kRegExterior), Clamp(Entry.Strength, 0.0f, 1.0f), PixelAngle,
             S.LodDetailFade > 0.5f && DetailTexel > 1.0e-3 ? (float)(1.0 / DetailTexel) : 0.0f,
             bIgnoreDepth ? 1.0f : 0.0f);
        // c79 executed-instruction budget (per step with this frame's point lights, per
        // exclusion zone and step, the device limit minus the per-pixel reserve), 1 /
        // exterior_shadow_fade_in.
        const bool bMacroShadow = S.ExteriorShadowDensity > 0.0f;
        Set4(RaymarchRegister(kRegBudget),
             kExecutedPerStep + kExecutedPerLightStep * (float)(LightCount),
             kExecutedPerExclusionStep,
             GExecutedLimit - kExecutedReserve,
             bMacroShadow ? 1.0f / (S.ExteriorShadowFadeIn > 1.0f ? S.ExteriorShadowFadeIn : 1.0f)
                          : 0.0f);
        // c80 the large-scale haze shadow: the haze extinction times exterior_shadow_density.
        Set4(RaymarchRegister(kRegMacroShadow),
             bMacroShadow ? Medium.HazeSigma * S.ExteriorShadowDensity : 0.0f, 0.0f, 0.0f, 0.0f);

        // c93..c96 opaque spheres drawn without depth before this pass (far planets and
        // moons): camera-relative centre, |centre|^2 - radius^2; 1e30 = unused.
        for (int Index = 0; Index < kMaxFarSpheres; ++Index)
        {
            if (Index < GFarSphereCount)
            {
                FFarSphereNote const& Note = GFarSpheres[Index];
                const double Rel[3] = {Note.Centre[0] - CamPos[0], Note.Centre[1] - CamPos[1],
                                       Note.Centre[2] - CamPos[2]};
                const double Radius = (double)(Note.Radius);
                Set4(RaymarchRegister(kRegFarSphere + Index), (float)(Rel[0]), (float)(Rel[1]),
                     (float)(Rel[2]),
                     (float)(Rel[0] * Rel[0] + Rel[1] * Rel[1] + Rel[2] * Rel[2] - Radius * Radius));
            }
            else
                Set4(RaymarchRegister(kRegFarSphere + Index), 0.0f, 0.0f, 0.0f, 1.0e30f);
        }
        // c97..c101 star glows drawn before this pass: centre, 1 / glow radius; distances (-1 =
        // unused); weights star_glow_weight x the sprite's alpha (0 = unused).
        float StarDist[kMaxStarGlows] = {-1.0f, -1.0f, -1.0f};
        float StarWeight[kMaxStarGlows] = {0.0f, 0.0f, 0.0f};
        const float GlowWeight = S.StarGlowWeight > 0.0f ? S.StarGlowWeight : 0.0f;
        for (int Index = 0; Index < GStarGlowCount && GlowWeight > 0.0f; ++Index)
        {
            FStarGlowNote const& Note = GStarGlows[Index];
            const double Rel[3] = {Note.Centre[0] - CamPos[0], Note.Centre[1] - CamPos[1],
                                   Note.Centre[2] - CamPos[2]};
            const double Dist = std::sqrt(Rel[0] * Rel[0] + Rel[1] * Rel[1] + Rel[2] * Rel[2]);
            if (!(Note.Radius > 1.0f) || !(Dist > 1.0))
                continue;
            Set4(RaymarchRegister(kRegStarGlow + Index), (float)(Rel[0]), (float)(Rel[1]),
                 (float)(Rel[2]), 1.0f / Note.Radius);
            StarDist[Index] = (float)(Dist);
            StarWeight[Index] = GlowWeight * Clamp(Note.Alpha, 0.0f, 1.0f);
        }
        Set4(RaymarchRegister(kRegStarDistance), StarDist[0], StarDist[1], StarDist[2], 0.0f);
        Set4(RaymarchRegister(kRegStarWeight), StarWeight[0], StarWeight[1], StarWeight[2], 0.0f);
        // c102 the asteroid cubes fading out at the end of the extended spawn distance (they
        // write depth while translucent). A pixel's depth is its asteroid's surface, up to about
        // a cube from the cube centre the alpha was computed for: the shader's fade ends one cube
        // earlier (never more of the pixel kept than the asteroid's alpha -- no silhouette), and
        // a pixel up to one cube beyond the end can still be such an asteroid.
        if (!bIgnoreDepth && GAsteroidFadeEnd > 0.0f && GAsteroidFadeBand > 0.0f)
            Set4(RaymarchRegister(kRegAsteroidFade), GAsteroidFadeEnd - GAsteroidCubeSize,
                 1.0f / GAsteroidFadeBand, GAsteroidFadeEnd + GAsteroidCubeSize, 0.0f);

        // The composite's c3: exposure, mode, white -- a plain pass-through when the ray march
        // already wrote display values (8-bit target, debug views 1 and 2).
        if (GbVolumeTargetFloat16 && !bShaderView)
            Set4(OutTonemap, Exposure, TonemapMode, White, 0.0f);
        else
            Set4(OutTonemap, 1.0f, 0.0f, 1.0f, 0.0f);

        *OutUpsampleThreshold = S.UpsampleDepthThreshold;
        return true;
    }

    FVolumeEntry* FindEntry(FNebulaEffect const* Nebula)
    {
        if (Nebula == nullptr)
            return nullptr;
        for (int Index = 0; Index < GEntryCount; ++Index)
        {
            if (GEntries[Index].Nebula == Nebula)
                return &GEntries[Index];
        }
        return nullptr;
    }

    // The volume draws the nebula at full strength (the exterior fade band keeps retail too).
    FVolumeEntry const* FullEntry(FNebulaEffect const* Nebula)
    {
        FVolumeEntry const* const Entry = FindEntry(Nebula);
        return Entry != nullptr && Entry->Strength >= 1.0f ? Entry : nullptr;
    }

    // One nebula: ray march into the low-resolution target, then the composite onto the back
    // buffer, in a pass of its own (the composite's blend states end with it). Near / Far:
    // the planes of the current projection. Returns false when nothing was composited.
    bool DrawEntry(FVolumeEntry const& Entry, ICamera* Camera, float Near, float Far,
                   bool bIgnoreDepth)
    {
        IRenderPipelineEx* const Ex = RenderEx::GetPipelineEx();
        IRPDrawEx* const DrawEx = RenderEx::GetDrawEx();
        FRPExTexture* SceneDepth = nullptr;
        if (Camera == nullptr || Ex == nullptr || DrawEx == nullptr || GbProgramFailed ||
            Ex->GetSceneDepth(&SceneDepth) != GR_OK)
            return false;
        unsigned long FullWidth = 0;
        unsigned long FullHeight = 0;
        unsigned long LowWidth = 0;
        unsigned long LowHeight = 0;
        if (Ex->GetExTextureSize(SceneDepth, &FullWidth, &FullHeight) != GR_OK ||
            Ex->GetExTextureSize(GVolumeTarget, &LowWidth, &LowHeight) != GR_OK)
            return false;

        // Composite constants: c0 ProjZ, c1 low-res size, c2 half full-res texel + threshold +
        // ignore depth, c3 tonemap (filled by BuildRaymarchConstants).
        memset(GCompositeConstants, 0, sizeof(GCompositeConstants));
        float UpsampleThreshold = 0.1f;
        if (!BuildRaymarchConstants(Entry, Camera, Near, Far, FullWidth, FullHeight, LowWidth,
                                    LowHeight, bIgnoreDepth, &UpsampleThreshold,
                                    &GCompositeConstants[12]))
            return false;
        memcpy(&GCompositeConstants[0], RaymarchRegister(0), sizeof(float) * 4);
        Set4(&GCompositeConstants[4], (float)(LowWidth), (float)(LowHeight),
             1.0f / (float)(LowWidth), 1.0f / (float)(LowHeight));
        Set4(&GCompositeConstants[8], 0.5f / (float)(FullWidth), 0.5f / (float)(FullHeight),
             UpsampleThreshold > 0.0f ? UpsampleThreshold : 0.0f, bIgnoreDepth ? 1.0f : 0.0f);

        FRPExSamplerState Point;
        Point.MinMagFilter = RPExFilterPoint;
        Point.MipFilter = RPExMipNone;
        Point.AddressU = RPExAddressClamp;
        Point.AddressV = RPExAddressClamp;
        Point.AddressW = RPExAddressClamp;
        FRPExSamplerState LinearWrap;
        LinearWrap.MinMagFilter = RPExFilterLinear;
        LinearWrap.MipFilter = RPExMipNone;
        LinearWrap.AddressU = RPExAddressWrap;
        LinearWrap.AddressV = RPExAddressWrap;
        LinearWrap.AddressW = RPExAddressWrap;
        FRPExSamplerState LinearClamp = LinearWrap;
        LinearClamp.AddressU = RPExAddressClamp;
        LinearClamp.AddressV = RPExAddressClamp;
        LinearClamp.AddressW = RPExAddressClamp;

        // Pass 1: ray march into the low-resolution target (rgb in-scattered light, a
        // transmittance); every pixel is written, no clear needed.
        FRPExPassDesc Pass;
        Pass.ColorTarget = GVolumeTarget;
        Pass.Flags = 0;
        Pass.ClearColor = 0;
        if (Ex->BeginExPass(&Pass) != GR_OK)
            return false;
        bool bOk = Ex->BindExProgram(GRaymarchProgram) == GR_OK;
        if (!bOk)
            GbProgramFailed = true;
        if (bOk)
            bOk = Ex->BindExTexture(0, SceneDepth, &Point) == GR_OK &&
                  Ex->BindExTexture(1, GNoiseVolume, &LinearWrap) == GR_OK;
        if (bOk)
        {
            Ex->SetExPixelConstants(0, GRaymarchConstants, kRaymarchRegisterCount);
            bOk = DrawEx->DrawFullscreenQuad() == GR_OK;
        }

        // Pass 2: composite onto the back buffer, dst = dst * T + tonemap(inscatter)
        // (ONE, SRCALPHA).
        if (bOk)
            bOk = Ex->SetExRenderTarget(nullptr, 0) == GR_OK;
        if (bOk)
        {
            bOk = Ex->BindExProgram(GCompositeProgram) == GR_OK;
            if (!bOk)
                GbProgramFailed = true;
        }
        if (bOk)
            bOk = Ex->BindExTexture(0, SceneDepth, &Point) == GR_OK &&
                  Ex->BindExTexture(1, GVolumeTarget, &LinearClamp) == GR_OK;
        if (bOk)
        {
            IRenderPipeline* const Pipe = DALib::Pipeline;
            Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
            Pipe->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            Pipe->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
            Pipe->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            Ex->SetExPixelConstants(0, GCompositeConstants, kCompositeRegisterCount);
            bOk = DrawEx->DrawFullscreenQuad() == GR_OK;
        }
        Ex->EndExPass();
        return bOk;
    }

    // Draws an entry once; the controller's draw decides the frame's real background.
    void DrawEntryOnce(FVolumeEntry& Entry, ICamera* Camera, float Near, float Far,
                       bool bIgnoreDepth)
    {
        if (Entry.bDrawn)
            return;
        Entry.bDrawn = true;
        const bool bOk = DrawEntry(Entry, Camera, Near, Far, bIgnoreDepth);
        if (!bOk && Entry.Nebula == GController)
            GbRealBackground = false;
    }

    // ---- lightning: strikes and the ship light ---------------------------------------------

    // Timing, colour and strength of a nebula's lightning, from its own INI (cli_nebula.cpp's
    // parsers): [Clouds] lightning_gap / lightning_duration / lightning_color /
    // lightning_intensity (the first block, raw +0x9c / +0xa0 / +0x90 / +0xdc) when the gap and
    // duration are set -- retail flashes each puff on its own timer, so the gap counts
    // kCloudLightningSources times -- else [BackgroundLightning] gap / duration / intensity
    // (+0x250 / +0x254 / +0x268), else 8 s / 0.4 s. The colour: lightning_colour, else the
    // [Clouds] lightning_color, [DynamicLightning] color (+0x280, when its duration +0x278 is
    // set), [BackgroundLightning] color (+0x25c), as a hue half toward white (retail's colours
    // are dim additive tints), else a pale blue. lightning_frequency_scale divides the gap,
    // lightning_duration_scale multiplies the duration.
    struct FLightningSource
    {
        double Gap;      // s; each next strike after Gap * (0.8 .. 1.5), retail's random gap
        double Duration; // s
        float Hue[3];    // brightest channel 1
        float Intensity;
    };

    float ColourPeak(float const* Colour)
    {
        float Peak = Colour[0] > Colour[1] ? Colour[0] : Colour[1];
        return Colour[2] > Peak ? Colour[2] : Peak;
    }

    void ReadLightningSource(FNebulaEffect const* Nebula, FNebulaVolumeSettings const& S,
                             FLightningSource* Out)
    {
        unsigned char const* const B = reinterpret_cast<unsigned char const*>(Nebula);
        Out->Gap = 0.0;
        Out->Duration = 0.0;
        Out->Intensity = 1.0f;
        float CloudColour[3] = {0.0f, 0.0f, 0.0f};
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        if (Sentinel != nullptr && Sentinel->Next != Sentinel && Sentinel->Next->Clouds != nullptr)
        {
            unsigned char const* const C = Sentinel->Next->Clouds->Raw;
            float Gap = 0.0f;
            float Duration = 0.0f;
            float Intensity = 0.0f;
            memcpy(&Gap, C + 0x9c, sizeof(float));
            memcpy(&Duration, C + 0xa0, sizeof(float));
            memcpy(&Intensity, C + 0xdc, sizeof(float));
            memcpy(CloudColour, C + 0x90, sizeof(CloudColour));
            if (Gap > 0.0f && Duration > 0.0f)
            {
                Out->Gap = (double)(Gap) / kCloudLightningSources;
                Out->Duration = (double)(Duration);
                Out->Intensity = Intensity > 0.0f ? Intensity : 1.0f;
            }
        }
        float BackGap = 0.0f;
        float BackDuration = 0.0f;
        float BackIntensity = 0.0f;
        float BackColour[3] = {0.0f, 0.0f, 0.0f};
        memcpy(&BackGap, B + 0x250, sizeof(float));
        memcpy(&BackDuration, B + 0x254, sizeof(float));
        memcpy(BackColour, B + 0x25c, sizeof(BackColour));
        memcpy(&BackIntensity, B + 0x268, sizeof(float));
        if (!(Out->Gap > 0.0) && BackGap > 0.0f && BackDuration > 0.0f)
        {
            Out->Gap = (double)(BackGap);
            Out->Duration = (double)(BackDuration);
            Out->Intensity = BackIntensity > 0.0f ? BackIntensity : 1.0f;
        }
        if (!(Out->Gap > 0.0))
        {
            Out->Gap = 8.0;
            Out->Duration = 0.4;
        }

        float DynamicDuration = 0.0f;
        float DynamicColour[3] = {0.0f, 0.0f, 0.0f};
        memcpy(&DynamicDuration, B + 0x278, sizeof(float));
        memcpy(DynamicColour, B + 0x280, sizeof(DynamicColour));
        float const* Tint = nullptr;
        bool bWhiten = true;
        if (ColourPeak(S.LightningColour) > 0.0f)
        {
            Tint = S.LightningColour;
            bWhiten = false;
        }
        else if (ColourPeak(CloudColour) > 0.0f)
            Tint = CloudColour;
        else if (DynamicDuration > 0.0f && ColourPeak(DynamicColour) > 0.0f)
            Tint = DynamicColour;
        else if (ColourPeak(BackColour) > 0.0f)
            Tint = BackColour;
        if (Tint != nullptr)
        {
            const float Peak = ColourPeak(Tint);
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                const float Hue = Clamp(Tint[Channel], 0.0f, Peak) / Peak;
                Out->Hue[Channel] = bWhiten ? 0.5f + 0.5f * Hue : Hue;
            }
        }
        else
        {
            Out->Hue[0] = 0.75f;
            Out->Hue[1] = 0.85f;
            Out->Hue[2] = 1.0f;
        }

        Out->Gap /= (double)(S.LightningFrequencyScale > 0.01f ? S.LightningFrequencyScale : 0.01f);
        Out->Duration *= (double)(S.LightningDurationScale > 0.01f ? S.LightningDurationScale : 0.01f);
        Out->Duration = Out->Duration < 0.08 ? 0.08 : (Out->Duration > 3.0 ? 3.0 : Out->Duration);
    }

    FRegisteredNebula* FindRegistered(FNebulaEffect const* Nebula)
    {
        for (size_t Index = 0; Index < GNebulae.size(); ++Index)
        {
            if (GNebulae[Index].Nebula == Nebula)
                return &GNebulae[Index];
        }
        return nullptr;
    }

    // A strike position for an entry: lightning_candidates random candidates, the one in the
    // densest cloud wins (the shader's base shape evaluated on the CPU, WorldBaseCloud, with
    // the outer shell's coverage rise and density, ShellProfileAt); never inside (or right at)
    // an exclusion zone. With the camera
    // inside the nebula the candidates lie lightning_min_distance .. lightning_max_distance
    // from it (bFar: lightning_max_distance .. lightning_far_distance, the distant flicker in
    // the haze), lightning_view_bias of them in (a little beyond) the view; outside it
    // anywhere in the zone, lightning_view_bias of them on the camera's side. BurstPos: around
    // the previous strike of a burst instead. False when nothing qualified: a near strike needs
    // lightning_min_density, a far or burst one only has to be inside the zone.
    bool PlaceStrike(FVolumeEntry const& Entry, ICamera* Camera, FNoiseFrame const& Frame,
                     double Radius, bool bFar, double const* BurstPos, double* OutPos,
                     float* OutCloud)
    {
        FNebulaVolumeSettings const& S = Entry.Settings;
        Universe::IZone const* const Zone = Entry.Nebula->Zone;
        float const* const CamPosF = Camera->GetPosition();
        const double CamPos[3] = {CamPosF[0], CamPosF[1], CamPosF[2]};
        float const* const View = Camera->GetViewTransform();
        const double TanY = std::tan(kPi / 180.0 * (double)(Camera->GetFovYDeg()));
        const double TanX = TanY * (double)(Camera->GetAspect());
        const Vector& ZonePos = Zone->GetPos();
        const Matrix& Orientation = Zone->GetOrientation();
        float Rows[3][3];
        ZoneLocalRows(Zone, Rows[0], Rows[1], Rows[2]);
        float Radii[3];
        ZoneRadii(Zone, Radii);
        FShellFrame Shell;
        ComputeShellFrame(Zone, S, &Shell);
        const double MinDistance = S.LightningMinDistance > 0.0f ? (double)(S.LightningMinDistance) : 0.0;
        double MaxDistance = (double)(S.LightningMaxDistance);
        if (!(MaxDistance > MinDistance + 1.0))
            MaxDistance = MinDistance + 1.0;
        double FarDistance = (double)(S.LightningFarDistance);
        if (!(FarDistance > 0.0))
        {
            FarDistance = (double)(Entry.MarchCap);
            if (FarDistance < 1.5 * MaxDistance)
                FarDistance = 1.5 * MaxDistance;
        }
        if (FarDistance < MaxDistance)
            FarDistance = MaxDistance;
        const float ViewBias = Clamp(S.LightningViewBias, 0.0f, 1.0f);

        // The camera's direction from the zone centre in the zone's unit space.
        double CamLocal[3];
        for (int Row = 0; Row < 3; ++Row)
            CamLocal[Row] = Rows[Row][0] * (CamPos[0] - (double)(ZonePos.x)) +
                            Rows[Row][1] * (CamPos[1] - (double)(ZonePos.y)) +
                            Rows[Row][2] * (CamPos[2] - (double)(ZonePos.z));
        const double CamLocalLength =
            std::sqrt(CamLocal[0] * CamLocal[0] + CamLocal[1] * CamLocal[1] + CamLocal[2] * CamLocal[2]);
        for (int Axis = 0; Axis < 3; ++Axis)
            CamLocal[Axis] = CamLocalLength > 1.0e-9 ? CamLocal[Axis] / CamLocalLength : 0.0;

        Exclusion::NebulaExclusionZoneMgr const* const Mgr = Entry.Nebula->ExclusionMgr;
        const int ZoneCount = Mgr != nullptr ? FNebulaVolumeExclusionAccess::Count(Mgr) : 0;
        int Candidates = (int)(S.LightningCandidates + 0.5f);
        Candidates = Candidates < 1 ? 1 : (Candidates > 32 ? 32 : Candidates);
        float Best = -1.0f;
        for (int Candidate = 0; Candidate < Candidates; ++Candidate)
        {
            double World[3];
            if (BurstPos != nullptr)
            {
                double Ball[3];
                RandomInBall(Ball);
                for (int Axis = 0; Axis < 3; ++Axis)
                    World[Axis] = BurstPos[Axis] + Ball[Axis] * Radius * 0.6;
            }
            else if (Entry.bInside)
            {
                double Dir[3];
                if (Random01() < ViewBias)
                {
                    // Through the view (view rows: world = x Row0 + y Row1 - Row2), a little
                    // beyond its edges.
                    const double X = (double)(RandomRange(-1.1f, 1.1f)) * TanX;
                    const double Y = (double)(RandomRange(-1.1f, 1.1f)) * TanY;
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Dir[Axis] = X * View[Axis] + Y * View[3 + Axis] - View[6 + Axis];
                    const double Length =
                        std::sqrt(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
                    if (!(Length > 1.0e-9))
                        continue;
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Dir[Axis] /= Length;
                }
                else
                    RandomDirection(Dir);
                const double R = (double)(Random01());
                const double Distance = bFar ? MaxDistance + (FarDistance - MaxDistance) * R
                                             : MinDistance + (MaxDistance - MinDistance) * std::sqrt(R);
                for (int Axis = 0; Axis < 3; ++Axis)
                    World[Axis] = CamPos[Axis] + Dir[Axis] * Distance;
            }
            else
            {
                double P[3];
                RandomInBall(P);
                const double Side = P[0] * CamLocal[0] + P[1] * CamLocal[1] + P[2] * CamLocal[2];
                if (Side < 0.0 && Random01() < ViewBias)
                {
                    for (int Axis = 0; Axis < 3; ++Axis)
                        P[Axis] -= 2.0 * Side * CamLocal[Axis];
                }
                for (int Row = 0; Row < 3; ++Row)
                {
                    const double Pos = Row == 0 ? (double)(ZonePos.x) : (Row == 1 ? (double)(ZonePos.y)
                                                                                 : (double)(ZonePos.z));
                    World[Row] = Pos + 0.95 * (Orientation.E[Row][0] * Radii[0] * P[0] +
                                               Orientation.E[Row][1] * Radii[1] * P[1] +
                                               Orientation.E[Row][2] * Radii[2] * P[2]);
                }
            }

            // Inside the zone, its outer shell as in the shader (displaced radius, thinning
            // medium, the clouds' coverage rising toward the surface).
            double Local[3];
            for (int Row = 0; Row < 3; ++Row)
                Local[Row] = Rows[Row][0] * (World[0] - (double)(ZonePos.x)) +
                             Rows[Row][1] * (World[1] - (double)(ZonePos.y)) +
                             Rows[Row][2] * (World[2] - (double)(ZonePos.z));
            double Outer = 1.0;
            const double Mask = ShellProfileAt(Shell, Local, &Outer);
            if (!(Mask > 0.0))
                continue;

            // Not in (or right at) an exclusion zone.
            bool bExcluded = false;
            for (int Index = 0; Index < ZoneCount && !bExcluded; ++Index)
            {
                Exclusion::NebulaExclusionZone const* const Excl =
                    FNebulaVolumeExclusionAccess::At(Mgr, Index);
                if (Excl == nullptr || Excl->ZonePtr == nullptr)
                    continue;
                float ExclRows[3][3];
                ZoneLocalRows(Excl->ZonePtr, ExclRows[0], ExclRows[1], ExclRows[2]);
                const Vector& ExclPos = Excl->ZonePtr->GetPos();
                double ExclSq = 0.0;
                for (int Row = 0; Row < 3; ++Row)
                {
                    const double L = ExclRows[Row][0] * (World[0] - (double)(ExclPos.x)) +
                                     ExclRows[Row][1] * (World[1] - (double)(ExclPos.y)) +
                                     ExclRows[Row][2] * (World[2] - (double)(ExclPos.z));
                    ExclSq += L * L;
                }
                bExcluded = ExclSq < 1.05 * 1.05;
            }
            if (bExcluded)
                continue;

            const float Cloud =
                WorldBaseCloud(Frame, World, (float)(Outer * Outer) * Shell.CloudRise) * (float)(Mask);
            if (Cloud > Best)
            {
                Best = Cloud;
                for (int Axis = 0; Axis < 3; ++Axis)
                    OutPos[Axis] = World[Axis];
            }
        }
        if (Best < 0.0f)
            return false;
        if (!bFar && BurstPos == nullptr && Best < S.LightningMinDensity)
            return false;
        *OutCloud = Best;
        return true;
    }

    // A new strike in a free slot, or in place of the least important live one (envelope x
    // peak x closeness to the camera) when the new one matters more; dropped otherwise.
    // Returns its lifetime (0 when dropped).
    float SpawnStrike(FVolumeEntry const& Entry, FLightningSource const& Source, double const* Pos,
                      float Cloud, double Radius, double Now, double const* CamPos)
    {
        FNebulaVolumeSettings const& S = Entry.Settings;
        const float Peak = Source.Intensity *
                           (S.LightningIntensityScale > 0.0f ? S.LightningIntensityScale : 0.0f) *
                           RandomRange(0.6f, 1.0f);
        const double RadiusSq = Radius * Radius;
        int Slot = -1;
        double Lowest = 1.0e30;
        for (int Index = 0; Index < kMaxStrikes; ++Index)
        {
            FLightningStrike const& Strike = GStrikes[Index];
            if (Strike.Nebula == nullptr)
            {
                Slot = Index;
                break;
            }
            double StrikePos[3];
            StrikePosition(Strike, StrikePos);
            double DistSq = 0.0;
            for (int Axis = 0; Axis < 3; ++Axis)
                DistSq += (StrikePos[Axis] - CamPos[Axis]) * (StrikePos[Axis] - CamPos[Axis]);
            const double StrikeRadiusSq = (double)(Strike.Radius) * (double)(Strike.Radius);
            const double Importance = (double)(Strike.Envelope * Strike.Peak) * StrikeRadiusSq /
                                      (DistSq + StrikeRadiusSq + 1.0);
            if (Importance < Lowest)
            {
                Lowest = Importance;
                Slot = Index;
            }
        }
        if (GStrikes[Slot].Nebula != nullptr)
        {
            double DistSq = 0.0;
            for (int Axis = 0; Axis < 3; ++Axis)
                DistSq += (Pos[Axis] - CamPos[Axis]) * (Pos[Axis] - CamPos[Axis]);
            if (!((double)(Peak) * RadiusSq / (DistSq + RadiusSq + 1.0) > Lowest))
                return 0.0f;
        }

        FLightningStrike& Strike = GStrikes[Slot];
        Strike.Nebula = Entry.Nebula;
        for (int Axis = 0; Axis < 3; ++Axis)
            Strike.Pos[Axis] = Pos[Axis];
        Strike.Start = Now;
        Strike.Peak = Peak;
        for (int Channel = 0; Channel < 3; ++Channel)
            Strike.Colour[Channel] = Source.Hue[Channel] * Peak;
        Strike.Cloud = Cloud;
        Strike.Radius = (float)(Radius);

        // 2..4 pulses: the first at once and strongest, the rest later in the duration, weaker,
        // shorter and a little displaced (lightning_pulse_jitter of the radius).
        const float Duration = (float)(Source.Duration);
        int Pulses = 2 + (int)(Random01() * 3.0f);
        Pulses = Pulses > kMaxStrikePulses ? kMaxStrikePulses : Pulses;
        Strike.PulseCount = Pulses;
        Strike.PulseStart[0] = 0.0f;
        Strike.PulseAmp[0] = 1.0f;
        Strike.PulseTau[0] = Duration * RandomRange(0.10f, 0.22f);
        for (int Pulse = 1; Pulse < Pulses; ++Pulse)
        {
            Strike.PulseStart[Pulse] = Duration * RandomRange(0.12f, 0.8f);
            Strike.PulseAmp[Pulse] = RandomRange(0.35f, 1.0f);
            Strike.PulseTau[Pulse] = Duration * RandomRange(0.05f, 0.15f);
        }
        for (int Pulse = 2; Pulse < Pulses; ++Pulse)
        {
            const float Start = Strike.PulseStart[Pulse];
            const float Amp = Strike.PulseAmp[Pulse];
            const float Tau = Strike.PulseTau[Pulse];
            int Hole = Pulse;
            while (Hole > 1 && Strike.PulseStart[Hole - 1] > Start)
            {
                Strike.PulseStart[Hole] = Strike.PulseStart[Hole - 1];
                Strike.PulseAmp[Hole] = Strike.PulseAmp[Hole - 1];
                Strike.PulseTau[Hole] = Strike.PulseTau[Hole - 1];
                --Hole;
            }
            Strike.PulseStart[Hole] = Start;
            Strike.PulseAmp[Hole] = Amp;
            Strike.PulseTau[Hole] = Tau;
        }
        const float Jitter = S.LightningPulseJitter > 0.0f ? S.LightningPulseJitter : 0.0f;
        float Lifetime = 0.0f;
        for (int Pulse = 0; Pulse < Pulses; ++Pulse)
        {
            Strike.PulseTau[Pulse] = Strike.PulseTau[Pulse] > 0.015f ? Strike.PulseTau[Pulse] : 0.015f;
            double Ball[3] = {0.0, 0.0, 0.0};
            if (Pulse > 0)
                RandomInBall(Ball);
            for (int Axis = 0; Axis < 3; ++Axis)
                Strike.PulseOffset[Pulse][Axis] = (float)(Ball[Axis] * Radius * (double)(Jitter));
            const float End = Strike.PulseStart[Pulse] + 5.0f * Strike.PulseTau[Pulse];
            Lifetime = End > Lifetime ? End : Lifetime;
        }
        Strike.Lifetime = Lifetime;
        Strike.Envelope = 0.0f;
        Strike.Pulse = 0;
        Strike.LightsShips = (int)(Clamp(S.LightningLightsShips, 0.0f, 2.0f) + 0.5f);
        Strike.ShipIntensity = S.LightningShipIntensity > 0.0f ? S.LightningShipIntensity : 0.0f;
        Strike.ShipDistance = S.LightningShipDistance;
        return Lifetime;
    }

    // The envelope at Now: the sum of the pulses (each rising in 8 ms, decaying with its tau),
    // at most 1.25; Strike.Pulse = the strongest one.
    float StrikeEnvelope(FLightningStrike& Strike, double Now)
    {
        const double Age = Now - Strike.Start;
        float Sum = 0.0f;
        float Strongest = 0.0f;
        Strike.Pulse = 0;
        for (int Pulse = 0; Pulse < Strike.PulseCount; ++Pulse)
        {
            const float Dt = (float)(Age - (double)(Strike.PulseStart[Pulse]));
            if (Dt < 0.0f)
                continue;
            const float Rise = Dt < 0.008f ? Dt / 0.008f : 1.0f;
            const float Value = Strike.PulseAmp[Pulse] * Rise * std::exp(-Dt / Strike.PulseTau[Pulse]);
            Sum += Value;
            if (Value > Strongest)
            {
                Strongest = Value;
                Strike.Pulse = Pulse;
            }
        }
        return Sum < 1.25f ? Sum : 1.25f;
    }

    // A nebula's [DynamicLightning] GameLight (+0x270; built by FNebulaEffect::Load only when
    // its duration +0x278 is above 0), or null.
    FGameLight* RetailNebulaLight(FNebulaEffect const* Nebula)
    {
        if (Nebula == nullptr)
            return nullptr;
        unsigned char const* const B = reinterpret_cast<unsigned char const*>(Nebula);
        FGameLight* Light = nullptr;
        float Duration = 0.0f;
        memcpy(&Light, B + 0x270, sizeof(Light));
        memcpy(&Duration, B + 0x278, sizeof(float));
        return Duration > 0.0f ? Light : nullptr;
    }

    // A lit GameLight (Flags bit 3) goes dark through its vtable slot 2, as retail does it.
    void SwitchLightOff(FGameLight* Light)
    {
        if (Light != nullptr && (Light->Flags & 0x08) != 0)
            GameLightVirtualDisable(Light);
    }

    // The volume's own ship light, built like retail's [DynamicLightning] light (directional,
    // range 500000, FNebulaEffect::Load) but not lit yet. Two references of our own:
    // GameLightReleaseAll (space teardown) drops one from every lit light.
    bool EnsureShipLight()
    {
        if (GShipLight != nullptr)
            return true;
        if (GbShipLightFailed)
            return false;
        void* const Memory = GameLightHeapAlloc(sizeof(FGameLight));
        if (Memory == nullptr)
        {
            GbShipLightFailed = true;
            return false;
        }
        FGameLight* const Light = GameLightConstruct(static_cast<FGameLight*>(Memory));
        EngineLightSetType(Light, 3, false);
        const float Zero[3] = {0.0f, 0.0f, 0.0f};
        EngineLightSetPosition(Light, Zero);
        const unsigned char White[3] = {0xff, 0xff, 0xff};
        EngineLightSetColorBytesAlt(Light, White);
        EngineLightSetRangeRaw(Light, kShipLightRange);
        GameLightAddRef(Light);
        GShipLight = Light;
        return true;
    }

    // Ships near a strike are lit from it: the strike that lights the camera's surroundings
    // most (envelope x R^2 / (d^2 + R^2), faded out at lightning_ship_distance, 0 = 3 radii, x
    // lightning_ship_intensity) drives a directional light from the strike toward the camera
    // in the strike's hue. The light: the [DynamicLightning] GameLight of the nebula the camera
    // is fully inside (its random flash -- NebulaAmbientTick -- stays out while the strikes
    // drive it: DrivesNebulaLight), else, for strikes of a nebula with lightning_lights_ships 2,
    // the volume's own light. A light no longer driven is left dark.
    void DriveShipLight(ICamera* Camera)
    {
        int Best = -1;
        float BestLevel = 0.0f;
        double BestPos[3] = {0.0, 0.0, 0.0};
        double CamPos[3] = {0.0, 0.0, 0.0};
        float CamPosF[3] = {0.0f, 0.0f, 0.0f};
        if (Camera != nullptr)
        {
            float const* const Position = Camera->GetPosition();
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                CamPosF[Axis] = Position[Axis];
                CamPos[Axis] = Position[Axis];
            }
            for (int Index = 0; Index < kMaxStrikes; ++Index)
            {
                FLightningStrike const& Strike = GStrikes[Index];
                if (Strike.Nebula == nullptr || Strike.LightsShips < 1)
                    continue;
                double Pos[3];
                StrikePosition(Strike, Pos);
                double DistSq = 0.0;
                for (int Axis = 0; Axis < 3; ++Axis)
                    DistSq += (Pos[Axis] - CamPos[Axis]) * (Pos[Axis] - CamPos[Axis]);
                const double RadiusSq = (double)(Strike.Radius) * (double)(Strike.Radius);
                const double MaxDistance = Strike.ShipDistance > 0.0f ? (double)(Strike.ShipDistance)
                                                                      : 3.0 * (double)(Strike.Radius);
                if (!(DistSq < MaxDistance * MaxDistance))
                    continue;
                const double Fade = 1.0 - DistSq / (MaxDistance * MaxDistance);
                const float Level = (float)((double)(Strike.Envelope * Strike.ShipIntensity) * RadiusSq /
                                            (DistSq + RadiusSq + 1.0) * Fade * Fade);
                if (Level > BestLevel)
                {
                    BestLevel = Level;
                    Best = Index;
                    for (int Axis = 0; Axis < 3; ++Axis)
                        BestPos[Axis] = Pos[Axis];
                }
            }
        }

        FNebulaEffect* Driven = nullptr;
        FVolumeEntry const* const ControllerEntry = FindEntry(GController);
        if (ControllerEntry != nullptr && ControllerEntry->bInside &&
            GController->GetFloat294() >= 1.0f &&
            ControllerEntry->Settings.LightningEnabled > 0.5f &&
            ControllerEntry->Settings.LightningLightsShips > 0.5f &&
            RetailNebulaLight(GController) != nullptr)
            Driven = GController;
        if (GDrivenNebula != nullptr && GDrivenNebula != Driven)
            SwitchLightOff(RetailNebulaLight(GDrivenNebula));
        GDrivenNebula = Driven;

        FGameLight* Target = nullptr;
        if (Driven != nullptr)
            Target = RetailNebulaLight(Driven);
        else if (Best >= 0 && GStrikes[Best].LightsShips >= 2 && EnsureShipLight())
            Target = GShipLight;
        if (GShipLight != nullptr && Target != GShipLight)
            SwitchLightOff(GShipLight);
        if (Target == nullptr)
            return;

        unsigned char Rgb[3] = {0, 0, 0};
        bool bLit = false;
        if (Best >= 0 && GStrikes[Best].Peak > 0.0f)
        {
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                const float Value =
                    Clamp(GStrikes[Best].Colour[Channel] / GStrikes[Best].Peak * BestLevel, 0.0f, 1.0f);
                Rgb[Channel] = (unsigned char)(Value * 255.0f + 0.5f);
                bLit = bLit || Rgb[Channel] > 1;
            }
        }
        if (!bLit)
        {
            SwitchLightOff(Target);
            return;
        }
        // The light travels from the strike toward the camera (D3D light direction).
        double Dir[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Dir[Axis] = CamPos[Axis] - BestPos[Axis];
        const double Length = std::sqrt(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
        const float DirF[3] = {Length > 1.0e-6 ? (float)(Dir[0] / Length) : 0.0f,
                               Length > 1.0e-6 ? (float)(Dir[1] / Length) : -1.0f,
                               Length > 1.0e-6 ? (float)(Dir[2] / Length) : 0.0f};
        EngineLightSetPosition(Target, CamPosF);
        EngineLightSetDirection(Target, DirF);
        EngineLightSetColorBytes(Target, Rgb);
        if ((Target->Flags & 0x08) == 0)
            GameLightEnable(Target);
    }

    // Once per frame, after the entries are chosen (BeginFrame): strikes of nebulae no longer
    // drawn, with the lightning off, or finished go; every drawn nebula with lightning_enabled
    // runs its scheduler (the next strike after its gap, sometimes a burst of 1..2 more right
    // after; [Debug] lightning_force: one after another, never far); the envelopes (with
    // lightning_flicker: a random dip per frame, like retail's rand * rand flashes) and the ship
    // light are updated. With no entries every strike goes and the lights go dark.
    void UpdateLightning(ICamera* Camera)
    {
        const double Now = SecondsNow();
        for (int Index = 0; Index < kMaxStrikes; ++Index)
        {
            FLightningStrike& Strike = GStrikes[Index];
            if (Strike.Nebula == nullptr)
                continue;
            FVolumeEntry const* const Entry = FindEntry(Strike.Nebula);
            if (Entry == nullptr || !(Entry->Settings.LightningEnabled > 0.5f) ||
                Now - Strike.Start > (double)(Strike.Lifetime))
                Strike.Nebula = nullptr;
        }

        if (Camera != nullptr)
        {
            float const* const CamPosF = Camera->GetPosition();
            const double CamPos[3] = {CamPosF[0], CamPosF[1], CamPosF[2]};
            for (int Index = 0; Index < GEntryCount; ++Index)
            {
                FVolumeEntry const& Entry = GEntries[Index];
                FNebulaVolumeSettings const& S = Entry.Settings;
                if (!(S.LightningEnabled > 0.5f))
                    continue;
                FRegisteredNebula* const Registered = FindRegistered(Entry.Nebula);
                if (Registered == nullptr)
                    continue;
                FLightningSource Source;
                ReadLightningSource(Entry.Nebula, S, &Source);
                if (GbLightningForce)
                {
                    if (Registered->NextStrike > Now + 2.0 * Source.Duration + 0.1)
                        Registered->NextStrike = Now;
                }
                else if (Registered->NextStrike < 0.0 || Now - Registered->NextStrike > 2.0 * Source.Gap)
                {
                    // Not armed yet, or not drawn for a long time: the first strike after a
                    // random part of a gap.
                    Registered->NextStrike = Now + Source.Gap * 1.5 * (double)(Random01());
                    Registered->BurstLeft = 0;
                }
                if (Now < Registered->NextStrike)
                    continue;

                float FogRgb[3];
                Entry.Nebula->CopyFogRgb(FogRgb);
                FMediumParams Medium;
                ComputeMedium(Entry.Nebula, S, &Medium);
                FCloudParams Clouds;
                ReadCloudParams(Entry.Nebula, FogRgb, &Clouds);
                FNoiseFrame Frame;
                ComputeNoiseFrame(Entry.Nebula, S, Medium, Clouds, Now, &Frame);
                const double Radius = StrikeRadius(S, Frame.CloudScale);
                const bool bBurst = Registered->BurstLeft > 0 && !GbLightningForce;
                const bool bFar = !bBurst && !GbLightningForce && Random01() < S.LightningFarChance;
                double Pos[3] = {0.0, 0.0, 0.0};
                float Cloud = 0.0f;
                if (!PlaceStrike(Entry, Camera, Frame, Radius, bFar,
                                 bBurst ? Registered->BurstPos : nullptr, Pos, &Cloud))
                {
                    // Nothing dense enough this time: try again shortly.
                    Registered->BurstLeft = 0;
                    Registered->NextStrike = Now + 0.25;
                    continue;
                }
                const float Lifetime = SpawnStrike(Entry, Source, Pos, Cloud, Radius, Now, CamPos);
                if (bBurst)
                    --Registered->BurstLeft;
                else if (!GbLightningForce && Random01() < S.LightningBurstChance)
                {
                    Registered->BurstLeft = Random01() < 0.4f ? 2 : 1;
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Registered->BurstPos[Axis] = Pos[Axis];
                }
                if (Registered->BurstLeft > 0)
                    Registered->NextStrike =
                        Now + (double)(Lifetime * RandomRange(0.5f, 1.0f) + RandomRange(0.05f, 0.35f));
                else if (GbLightningForce)
                    Registered->NextStrike = Now + (double)(Lifetime) * 1.1 + 0.05;
                else
                    Registered->NextStrike = Now + Source.Gap * (double)(RandomRange(0.8f, 1.5f));
            }
        }

        for (int Index = 0; Index < kMaxStrikes; ++Index)
        {
            FLightningStrike& Strike = GStrikes[Index];
            if (Strike.Nebula == nullptr)
                continue;
            FVolumeEntry const* const Entry = FindEntry(Strike.Nebula);
            const float Flicker =
                Entry != nullptr ? Clamp(Entry->Settings.LightningFlicker, 0.0f, 1.0f) : 0.0f;
            Strike.Envelope = StrikeEnvelope(Strike, Now) * (1.0f - Flicker * 0.6f * Random01());
        }
        DriveShipLight(Camera);
    }
}

void NebulaVolume::RegisterNebula(FNebulaEffect* Nebula, char const* File)
{
    if (Nebula == nullptr)
        return;
    FRegisteredNebula Entry;
    Entry.Nebula = Nebula;
    BaseName(Entry.File, sizeof(Entry.File), File);
    Entry.NextStrike = -1.0;
    Entry.BurstLeft = 0;
    Entry.BurstPos[0] = 0.0;
    Entry.BurstPos[1] = 0.0;
    Entry.BurstPos[2] = 0.0;
    for (size_t Index = 0; Index < GNebulae.size(); ++Index)
    {
        if (GNebulae[Index].Nebula == Nebula)
        {
            GNebulae[Index] = Entry;
            return;
        }
    }
    GNebulae.push_back(Entry);
}

void NebulaVolume::UnregisterNebula(FNebulaEffect* Nebula)
{
    for (size_t Index = 0; Index < GNebulae.size(); ++Index)
    {
        if (GNebulae[Index].Nebula == Nebula)
        {
            GNebulae.erase(GNebulae.begin() + (std::ptrdiff_t)(Index));
            break;
        }
    }
    for (int Index = 0; Index < GEntryCount; ++Index)
    {
        if (GEntries[Index].Nebula != Nebula)
            continue;
        for (int Next = Index + 1; Next < GEntryCount; ++Next)
            GEntries[Next - 1] = GEntries[Next];
        --GEntryCount;
        break;
    }
    if (GController == Nebula)
    {
        GController = nullptr;
        GbRealBackground = false;
        GControllerMarchCap = 0.0f;
    }
    // Its strikes go; its [DynamicLightning] light is NebulaDestroy's to release.
    for (int Index = 0; Index < kMaxStrikes; ++Index)
    {
        if (GStrikes[Index].Nebula == Nebula)
            GStrikes[Index].Nebula = nullptr;
    }
    if (GDrivenNebula == Nebula)
        GDrivenNebula = nullptr;
    // The system is being left (StarSysLeave destroys every nebula after GameLightLeaveSystem):
    // the volume's own ship light goes dark, off the GameLight list before the space teardown.
    if (GNebulae.empty())
        SwitchLightOff(GShipLight);
}

void NebulaVolume::LoadSettings(char const* IniPath)
{
    GSettings = DefaultSettings();
    GOverrides.clear();
    GNebulaView = 0;
    GbLightningForce = false;
    GbLightningShowPositions = false;
    // resolution_scale may have changed: a failed render target is tried again.
    GbTargetFailed = false;

    // Pass 1: [Nebula] and [Debug] nebula_view. Pass 2: [NebulaOverride], each starting from
    // the [Nebula] values, so the section order in the file does not matter.
    INI_Reader Reader;
    if (IniPath == nullptr || !Reader.open(IniPath, false))
        return;
    while (Reader.read_header())
    {
        if (Reader.is_header("Debug"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("nebula_view"))
                    GNebulaView = (int)(Clamp((float)(Reader.get_value_float(0)), 0.0f, 4.0f) + 0.5f);
                else if (Reader.is_value("lightning_force"))
                    GbLightningForce = Reader.get_value_float(0) > 0.5f;
                else if (Reader.is_value("lightning_show_positions"))
                    GbLightningShowPositions = Reader.get_value_float(0) > 0.5f;
            }
            continue;
        }
        if (!Reader.is_header("Nebula"))
            continue;
        while (Reader.read_value())
            ApplySettingKey(&GSettings, Reader);
    }
    Reader.close();

    if (!Reader.open(IniPath, false))
        return;
    while (Reader.read_header())
    {
        if (!Reader.is_header("NebulaOverride"))
            continue;
        FNebulaOverride Override;
        Override.File[0] = '\0';
        Override.Settings = GSettings;
        while (Reader.read_value())
        {
            if (Reader.is_value("file"))
                BaseName(Override.File, sizeof(Override.File), Reader.get_value_string(0));
            else
                ApplySettingKey(&Override.Settings, Reader);
        }
        if (Override.File[0] != '\0')
            GOverrides.push_back(Override);
        else
            FDUMP(0x100002, "%s(%d) : WARNING:General:[NebulaOverride] without file = ignored",
                  kNebulaVolumeSource, __LINE__);
    }
    Reader.close();
    FDUMP(0x100003, "%s(%d) : NOTICE:General:nebula volume settings: enabled %d, %u override(s), "
          "view %d", kNebulaVolumeSource, __LINE__, GSettings.Enabled > 0.5f ? 1 : 0,
          (unsigned int)(GOverrides.size()), GNebulaView);
}

void NebulaVolume::OnProgramsReloaded()
{
    // The next frame tries the programs again; a bind that still fails switches the volume off
    // again (Draw), a compile that failed keeps logging its errors in RP9.
    GbProgramFailed = false;
}

void NebulaVolume::Shutdown()
{
    IRenderPipelineEx* const Ex = RenderEx::GetPipelineEx();
    if (Ex != nullptr)
    {
        if (GRaymarchProgram != nullptr)
            Ex->DestroyExProgram(GRaymarchProgram);
        if (GCompositeProgram != nullptr)
            Ex->DestroyExProgram(GCompositeProgram);
        if (GNoiseVolume != nullptr)
            Ex->DestroyExTexture(GNoiseVolume);
        if (GVolumeTarget != nullptr)
            Ex->DestroyExTexture(GVolumeTarget);
    }
    GRaymarchProgram = nullptr;
    GCompositeProgram = nullptr;
    GNoiseVolume = nullptr;
    GVolumeTarget = nullptr;
    GVolumeTargetScale = 0.0f;
    GbVolumeTargetFloat16 = false;
    GbProgramsCreated = false;
    GbProgramFailed = false;
    GbNoiseFailed = false;
    GbTargetFailed = false;
    GbCapsQueried = false;
    GbCapsOk = false;
    GbBaseHistogramValid = false;
    GEntryCount = 0;
    GbRealBackground = false;
    GController = nullptr;
    GControllerMarchCap = 0.0f;
    GMaxAsteroidReach = 0.0f;
    std::vector<unsigned long>().swap(GNoiseTexels);

    // Lightning: the strikes go; the own ship light is switched off and its references dropped
    // (the last one destroys it, removing its D3D light from the pipeline, which is still
    // alive here -- RenderEx::Shutdown runs before the pipeline goes).
    for (int Index = 0; Index < kMaxStrikes; ++Index)
        GStrikes[Index].Nebula = nullptr;
    GDrivenNebula = nullptr;
    if (GShipLight != nullptr)
    {
        SwitchLightOff(GShipLight);
        while (GameLightRelease(GShipLight) > 0)
        {
        }
        GShipLight = nullptr;
    }
    GbShipLightFailed = false;
}

namespace
{
    // BeginFrame's first half: the nebulae drawn this frame (GEntries) and their frame values.
    void SelectFrameEntries(ICamera* Camera, FNebulaEffect* Controller, int Enclosure,
                            bool bNebulaOption, float SceneFar)
    {
        GEntryCount = 0;
        GbRealBackground = false;
        GController = Controller;
        GControllerMarchCap = 0.0f;
        GMaxAsteroidReach = 0.0f;
        GFarSphereCount = 0;
        GStarGlowCount = 0;
        GAsteroidFadeEnd = 0.0f;
        GAsteroidFadeBand = 0.0f;
        GAsteroidCubeSize = 0.0f;
        (void)Enclosure; // the controller's own immersion (+0x294) says the same, per nebula
        if (Camera == nullptr || !bNebulaOption || !RenderEx::IsAvailable() || GbProgramFailed ||
            GNebulae.empty())
            return;

        // Candidates, nearest first, at most max_nebulae ([Nebula], global): every nebula the
        // camera is inside (immersion +0x294 > 0, FNebulaEffect::Update), and with exterior
        // those whose surface is within exterior_max_distance. No allocation: insertion into
        // GEntries.
        float const* const CamPosF = Camera->GetPosition();
        const double CamPos[3] = {CamPosF[0], CamPosF[1], CamPosF[2]};
        GFrameCamPos[0] = CamPos[0];
        GFrameCamPos[1] = CamPos[1];
        GFrameCamPos[2] = CamPos[2];
        int MaxCount = (int)(Clamp(GSettings.MaxNebulae, 1.0f, (float)(kMaxNebulae)) + 0.5f);
        for (size_t Index = 0; Index < GNebulae.size(); ++Index)
        {
            FNebulaEffect* const Nebula = GNebulae[Index].Nebula;
            if (Nebula == nullptr || Nebula->FlagLoaded == 0 || Nebula->Zone == nullptr)
                continue;
            FNebulaVolumeSettings const& Settings = SettingsFor(Nebula);
            if (!(Settings.Enabled > 0.5f))
                continue;
            const bool bInside = Nebula->GetFloat294() > 0.0f;
            // The drawn bound is the halo's end plus the shell noise, not the zone surface:
            // the distances (exterior fade, far bucket, the key light's reference) count from it.
            FShellFrame Shell;
            ComputeShellFrame(Nebula->Zone, Settings, &Shell);
            double Distance = 0.0;
            float Strength = 1.0f;
            if (!bInside)
            {
                if (!(Settings.Exterior > 0.5f))
                    continue;
                Distance = SurfaceDistance(Nebula->Zone, CamPos, (double)(Shell.SpanScale));
                const double MaxDistance = (double)(Settings.ExteriorMaxDistance);
                if (!(Distance < MaxDistance))
                    continue;
                const double Fade = Settings.ExteriorFadeDistance > 1.0f
                                        ? (double)(Settings.ExteriorFadeDistance)
                                        : 1.0;
                const double Remaining = (MaxDistance - Distance) / Fade;
                Strength = Remaining >= 1.0 ? 1.0f : (float)(Remaining);
                if (!(Strength > 0.001f))
                    continue;
            }
            // Insertion by distance.
            int Slot = GEntryCount;
            if (GEntryCount == MaxCount)
            {
                if (!(Distance < (double)(GEntries[GEntryCount - 1].SurfaceDistance)))
                    continue;
                Slot = GEntryCount - 1;
            }
            else
                ++GEntryCount;
            while (Slot > 0 && (double)(GEntries[Slot - 1].SurfaceDistance) > Distance)
            {
                GEntries[Slot] = GEntries[Slot - 1];
                --Slot;
            }
            FVolumeEntry& Entry = GEntries[Slot];
            Entry.Nebula = Nebula;
            Entry.Settings = Settings;
            Entry.SpanScale = Shell.SpanScale;
            Entry.SurfaceDistance = (float)(Distance);
            Entry.Strength = Strength;
            Entry.MarchCap = 0.0f;
            Entry.AsteroidHazeReach = 0.0f;
            // Asteroids are seen through the thin outer shell: their spawn reach counts from the
            // shell's half-density surface (undisplaced), also when the camera sits in the shell.
            Entry.AsteroidSurfaceDistance =
                (float)(SurfaceDistance(Nebula->Zone, CamPos, 1.0 - 0.5 * (double)(Shell.Width)));
            Entry.bInside = bInside;
            Entry.bBackground = false;
            Entry.bDrawn = false;
        }
        if (GEntryCount == 0)
            return;

        IRenderPipelineEx* const Ex = RenderEx::GetPipelineEx();
        FRPExTexture* SceneDepth = nullptr;
        unsigned long Width = 0;
        unsigned long Height = 0;
        // The render target follows the nearest nebula's resolution_scale. A render target lost
        // with the device (and not recreated) has no size.
        if (!QueryCaps() || Ex->GetSceneDepth(&SceneDepth) != GR_OK ||
            !EnsureResources(Ex, GEntries[0].Settings) ||
            Ex->GetExTextureSize(GVolumeTarget, &Width, &Height) != GR_OK)
        {
            GEntryCount = 0;
            return;
        }

        for (int Index = 0; Index < GEntryCount; ++Index)
        {
            FVolumeEntry& Entry = GEntries[Index];
            FMediumParams Medium;
            ComputeMedium(Entry.Nebula, Entry.Settings, &Medium);
            Entry.MarchCap = Medium.MarchCap;
            Entry.AsteroidHazeReach = AsteroidHazeReach(Entry.Nebula, Entry.Settings, Medium,
                                                        Entry.bInside);
            // Beyond the opaque scene's far plane every drawn object is in front of the nebula:
            // it is drawn with the far bucket (its ZItemNebula node), in painter order with the
            // far solars that bucket draws without depth. Only the non-controller nebulae have a
            // node.
            Entry.bBackground = !Entry.bInside && Entry.Nebula != Controller &&
                                (double)(Entry.SurfaceDistance) > (double)(SceneFar);
            if (Entry.Settings.AsteroidSpawn > 0.5f)
            {
                float Reach = Entry.AsteroidSurfaceDistance + Entry.AsteroidHazeReach;
                const float Cap = Entry.Settings.AsteroidMaxSpawnDistance;
                Reach = Reach < Cap ? Reach : Cap;
                if (Reach > GMaxAsteroidReach)
                    GMaxAsteroidReach = Reach;
            }
            if (Entry.Nebula == Controller && Entry.bInside)
            {
                GControllerMarchCap = Entry.MarchCap;
                GbRealBackground = Entry.Settings.RealBackground > 0.5f;
            }
        }
    }
}

void NebulaVolume::BeginFrame(ICamera* Camera, FNebulaEffect* Controller, int Enclosure,
                              bool bNebulaOption, float SceneFar)
{
    SelectFrameEntries(Camera, Controller, Enclosure, bNebulaOption, SceneFar);
    // Lightning: strikes of the drawn nebulae, the ship light (with no entry -- RP8, the
    // volume off, nothing in range -- every strike goes and a light the strikes drove is left
    // dark for retail).
    UpdateLightning(Camera);
}

bool NebulaVolume::DrivesNebulaLight(FNebulaEffect const* Nebula)
{
    return Nebula != nullptr && Nebula == GDrivenNebula;
}

bool NebulaVolume::IsActive()
{
    return GEntryCount > 0;
}

bool NebulaVolume::IsActiveFor(FNebulaEffect const* Nebula)
{
    return FindEntry(Nebula) != nullptr;
}

bool NebulaVolume::ReplacesObjectFog(FNebulaEffect const* Nebula)
{
    FVolumeEntry const* const Entry = FullEntry(Nebula);
    return Entry != nullptr && Entry->Settings.ReplaceObjectFog > 0.5f;
}

bool NebulaVolume::ReplacesFogOverlay(FNebulaEffect const* Nebula)
{
    FVolumeEntry const* const Entry = FindEntry(Nebula);
    return Entry != nullptr && Entry->Settings.ReplaceFogOverlay > 0.5f;
}

bool NebulaVolume::ReplacesClouds(FNebulaEffect const* Nebula)
{
    FVolumeEntry const* const Entry = FindEntry(Nebula);
    return Entry != nullptr && Entry->Settings.ReplaceClouds > 0.5f;
}

bool NebulaVolume::ReplacesExteriorBits(FNebulaEffect const* Nebula)
{
    FVolumeEntry const* const Entry = FullEntry(Nebula);
    return Entry != nullptr && Entry->Settings.ReplaceExteriorBits > 0.5f;
}

bool NebulaVolume::ReplacesFillSheets(FNebulaEffect const* Nebula)
{
    FVolumeEntry const* const Entry = FullEntry(Nebula);
    return Entry != nullptr && Entry->Settings.ReplaceFillSheets > 0.5f;
}

bool NebulaVolume::ShowsRealBackground()
{
    return GbRealBackground;
}

float NebulaVolume::InsideFarPlane(float RetailFar, float MaxFar)
{
    FVolumeEntry const* const Entry = FindEntry(GController);
    if (Entry == nullptr || !Entry->bInside || !(Entry->Settings.ExtendFarPlane > 0.5f))
        return RetailFar;
    // 1.5 march caps: the haze has hidden everything there (march_end_transmittance^1.5), so
    // objects do not pop in at the plane; and past the extended asteroid spawn distance, so
    // the asteroid cubes are not cut by the plane.
    float Wanted = GControllerMarchCap * 1.5f;
    const float AsteroidFar = GMaxAsteroidReach * 1.1f;
    if (AsteroidFar > Wanted)
        Wanted = AsteroidFar;
    if (Wanted > MaxFar)
        Wanted = MaxFar;
    return Wanted > RetailFar ? Wanted : RetailFar;
}

float NebulaVolume::AsteroidSpawnReach(Universe::IZone const* FieldZone, float* OutFadeFraction)
{
    if (OutFadeFraction != nullptr)
        *OutFadeFraction = 0.0f;
    if (FieldZone == nullptr || GEntryCount == 0)
        return 0.0f;
    const Vector& FieldPos = FieldZone->GetPos();
    const double FieldRadius = (double)(FieldZone->GetRadius());
    float Best = 0.0f;
    for (int Index = 0; Index < GEntryCount; ++Index)
    {
        FVolumeEntry const& Entry = GEntries[Index];
        if (!(Entry.Settings.AsteroidSpawn > 0.5f))
            continue;
        // The field and the nebula overlap (bounding spheres).
        Universe::IZone const* const Zone = Entry.Nebula->Zone;
        const Vector& Pos = Zone->GetPos();
        const double Dx = (double)(FieldPos.x) - (double)(Pos.x);
        const double Dy = (double)(FieldPos.y) - (double)(Pos.y);
        const double Dz = (double)(FieldPos.z) - (double)(Pos.z);
        const double Reach = FieldRadius + (double)(Zone->GetRadius()) * (double)(Entry.SpanScale);
        if (Dx * Dx + Dy * Dy + Dz * Dz > Reach * Reach)
            continue;
        // Outside the nebula (or in its thin outer shell) the asteroids behind its surface are
        // seen through the camera's distance to the shell's half-density surface plus the haze
        // length.
        float Spawn = Entry.AsteroidSurfaceDistance + Entry.AsteroidHazeReach;
        const float Cap = Entry.Settings.AsteroidMaxSpawnDistance;
        Spawn = Spawn < Cap ? Spawn : Cap;
        if (Spawn > Best)
        {
            Best = Spawn;
            if (OutFadeFraction != nullptr)
                *OutFadeFraction = Clamp(Entry.Settings.AsteroidFadeFraction, 0.0f, 0.9f);
        }
    }
    return Best;
}

void NebulaVolume::NoteStarGlow(float const* WorldPos, float Radius, float Alpha)
{
    if (GEntryCount == 0 || WorldPos == nullptr || !(Radius > 0.0f) || !(Alpha > 0.0f))
        return;
    // The same star drawn twice in a frame (its solar's draw and the burn-through) is one glow:
    // the larger sprite and alpha.
    for (int Index = 0; Index < GStarGlowCount; ++Index)
    {
        FStarGlowNote& Note = GStarGlows[Index];
        if (Note.Centre[0] == (double)(WorldPos[0]) && Note.Centre[1] == (double)(WorldPos[1]) &&
            Note.Centre[2] == (double)(WorldPos[2]))
        {
            if (Radius > Note.Radius)
                Note.Radius = Radius;
            if (Alpha > Note.Alpha)
                Note.Alpha = Alpha;
            return;
        }
    }
    if (GStarGlowCount >= kMaxStarGlows)
        return;
    FStarGlowNote& Note = GStarGlows[GStarGlowCount++];
    Note.Centre[0] = WorldPos[0];
    Note.Centre[1] = WorldPos[1];
    Note.Centre[2] = WorldPos[2];
    Note.Radius = Radius;
    Note.Alpha = Alpha;
}

void NebulaVolume::NoteOpaqueSphere(float const* WorldCentre, float Radius)
{
    if (GEntryCount == 0 || WorldCentre == nullptr || !(Radius > 0.0f))
        return;
    const double Dx = (double)(WorldCentre[0]) - GFrameCamPos[0];
    const double Dy = (double)(WorldCentre[1]) - GFrameCamPos[1];
    const double Dz = (double)(WorldCentre[2]) - GFrameCamPos[2];
    const double Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    // A camera inside the sphere: the scene depth has it (the shader ignores it too).
    if (!(Dist > (double)(Radius)))
        return;
    const float Angular = (float)((double)(Radius) / Dist);
    for (int Index = 0; Index < GFarSphereCount; ++Index)
    {
        FFarSphereNote const& Note = GFarSpheres[Index];
        if (Note.Centre[0] == (double)(WorldCentre[0]) &&
            Note.Centre[1] == (double)(WorldCentre[1]) &&
            Note.Centre[2] == (double)(WorldCentre[2]))
            return;
    }
    // Full: the largest on screen are kept (the smallest one is replaced).
    int Slot = GFarSphereCount;
    if (GFarSphereCount >= kMaxFarSpheres)
    {
        Slot = 0;
        for (int Index = 1; Index < GFarSphereCount; ++Index)
        {
            if (GFarSpheres[Index].Angular < GFarSpheres[Slot].Angular)
                Slot = Index;
        }
        if (!(Angular > GFarSpheres[Slot].Angular))
            return;
    }
    else
        ++GFarSphereCount;
    FFarSphereNote& Note = GFarSpheres[Slot];
    Note.Centre[0] = WorldCentre[0];
    Note.Centre[1] = WorldCentre[1];
    Note.Centre[2] = WorldCentre[2];
    Note.Radius = Radius;
    Note.Angular = Angular;
}

void NebulaVolume::NoteAsteroidFade(float FadeEnd, float FadeBand, float CubeSize)
{
    if (GEntryCount == 0 || !(FadeEnd > 0.0f) || !(FadeBand > 0.0f))
        return;
    // Several fields: the farthest fade (one band per pass).
    if (FadeEnd > GAsteroidFadeEnd)
    {
        GAsteroidFadeEnd = FadeEnd;
        GAsteroidFadeBand = FadeBand;
        GAsteroidCubeSize = CubeSize > 0.0f ? CubeSize : 0.0f;
    }
}

void NebulaVolume::DrawBackground(FNebulaEffect* Nebula, ICamera* Camera)
{
    FVolumeEntry* const Entry = FindEntry(Nebula);
    if (Entry == nullptr || !Entry->bBackground || Entry->bDrawn || Camera == nullptr)
        return;
    DrawEntryOnce(*Entry, Camera, Camera->GetZNear(), Camera->GetZFar(), true);
}

void NebulaVolume::Draw(ICamera* Camera, float Near, float Far)
{
    if (GEntryCount == 0)
        return;
    // Farthest first (the entries are nearest first), each over what is behind it; an entry
    // meant for the far bucket that the bucket did not draw (no node this frame) is drawn here
    // with the scene depth like the rest. Without a camera nothing is drawn and the frame's
    // real background goes (the clear colour must match what is on screen).
    if (Camera == nullptr)
        GbRealBackground = false;
    else
    {
        for (int Index = GEntryCount - 1; Index >= 0; --Index)
            DrawEntryOnce(GEntries[Index], Camera, Near, Far, false);
    }
    // The frame ends here: the Replaces* gates and AsteroidSpawnReach answer false / 0 until
    // the next BeginFrame; ShowsRealBackground stays for the clear colour.
    GEntryCount = 0;
}

#endif // FREELANCER_RP9
