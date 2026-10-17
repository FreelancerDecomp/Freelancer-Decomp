// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Volumetric nebula, pass 1: the ray march (port-only; freelancer.exe Client/nebula_volume.cpp
// fills every constant from the nebula's INI values and DATA_EXTRA/VOLUMETRIC/volumetric.ini
// [Nebula]). Drawn as a fullscreen quad into a reduced-resolution target, once per nebula:
//   rgb = light scattered toward the camera (HDR on an FP16 target), a = transmittance.
// pass 2 (nebula_composite.hlsl) does back buffer = scene * a + tonemap(rgb).
// Everything is camera-relative: the camera sits at the origin, world axes.
//
// The same code draws the nebula from inside and from outside: the ray starts where it enters
// the zone (the camera itself when inside) and the march cap is measured from there, so
// crossing the surface changes nothing but the geometry. From outside the exterior constants
// add a level of detail (fewer steps, fewer shadow samples and faded edge detail with the
// distance to the zone entry), a large-scale shadow of the haze toward the key light (the far
// side of the nebula darker) and the fade-out at exterior_max_distance; all three are exactly
// off when the ray starts at the camera.
//
// The medium has two parts:
//   haze   -- thin and uniform inside the zone, coloured by [Fog] color, lit by the ambient,
//             the key light (shadowed by the clouds: light shafts) and its own glow;
//   clouds -- dense masses cut out of the base noise by a coverage threshold (empty corridors
//             between them), their edges eroded by the detail noise, the domain warped.
// Both thin out over a wide outer shell whose radius is displaced by low-frequency noise (the
// clouds also get sparse there: the coverage threshold rises toward the surface), and a thin
// haze halo reaches beyond the surface; from outside the shell is thinner still
// (exterior_shell_density), so the edge is soft, irregular and see-through. Far away the
// clouds' coverage ramp widens and their edge detail fades: soft masses, not blotches.
// Light is scattered, not emitted: albedo x (key light x two-lobe phase x shadow x powder +
// ambient x occlusion), with a multiple-scattering approximation (Wrenninge 2013: octaves with
// a weaker extinction and phase) so thick clouds are not black, plus a small self-glow.
// Lightning: up to 4 point lights (strikes placed inside the clouds by the CPU) scatter their
// light in clouds and haze near them (a diffusion falloff through the clouds) and show a small
// emissive core (NebulaPointLight).
// Register pressure: ps_3_0 has 32 temp registers and this shader uses all of them; loop
// invariants are precomputed on the CPU (see c19.w) and no array is indexed dynamically.
// See docs/re/rp9.md, "Volumetric nebula".

#include "nebula_volume.hlsli"

#define NEBULA_MAX_STEPS 128
// The key-light shadow ray: fixed steps; volumetric.ini shadow_steps (0..6) gives the trailing
// ones zero weight. Far away (level of detail) the 3 pair-merged samples of ShadowStepHalf.
#define NEBULA_SHADOW_STEPS 6
#define NEBULA_SHADOW_STEPS_HALF 3
#define NEBULA_MAX_EXCLUSIONS 8
// Point lights (lightning strikes inside the clouds, nebula_volume.cpp fills them; count in
// Misc.y, 0 = no light work beyond a few instructions per step). 0 compiles them out.
#ifndef NEBULA_POINT_LIGHTS
#define NEBULA_POINT_LIGHTS 1
#endif
#define NEBULA_MAX_POINT_LIGHTS 4

// s0: the INTZ scene depth (point, clamp). s1: the noise volume (linear, wrap).
sampler2D SceneDepth : register(s0);
sampler3D Noise : register(s1);

float4 ProjZ : register(c0);         // projection z/w rows (RPExLinearDepth)
float4 ViewRect : register(c1);      // camera viewport in target UV: x0, y0, width, height
float4 ProjInv : register(c2);       // x 1/ScaleX, y 1/ScaleY, zw half a full-res texel
float4 CamRow[3] : register(c3);     // camera view rotation rows (world -> view, view looks -z)
float4 ZoneCentre : register(c6);    // xyz nebula zone centre, w unused (was the edge softness;
                                     // the shell width in c88 replaces it)
float4 ZoneRow[3] : register(c7);    // zone local axis / radius, per row
float4 March : register(c10);        // x steps, y march cap (m from the zone entry), z step
                                     // distribution, w jitter
float4 Medium : register(c11);       // x haze sigma (1/m), y cloud sigma at density 1, z clear
                                     // radius ([Fog] near), w debug view (1 cloud, 2 shadow)
float4 NoiseSpace : register(c12);   // xyz zone up axis (unit), w layer stretch - 1
float4 BaseNoise : register(c13);    // xyz base tile offset (camera + drift), w 1/base tile (m)
float4 DetailNoise : register(c14);  // xyz detail tile offset, w 1/detail tile
float4 WarpNoise : register(c15);    // xyz warp tile offset, w 1/warp tile
float4 Shape : register(c16);        // x coverage threshold, y 1/ramp width, z erosion,
                                     // w wispiness
float4 ShapeMore : register(c17);    // x warp amplitude (m), yz cloud near fade range (m),
                                     // w haze tail to the zone surface (0/1)
// The .w of c19 and c22 hold reciprocals of other constants: loop invariants computed in the
// shader would each hold a temp register through the march (ps_3_0 has 32, all in use). The
// same reason puts other derived values into constants (marked "see c19.w").
float4 HazeAlbedo : register(c18);   // rgb haze albedo, w unused
float4 HazeEmission : register(c19); // rgb haze self-glow, w 1 / [Fog] near (0 = no clear radius)
float4 CloudEmission : register(c20); // rgb cloud self-glow, w powder strength
float4 PuffColourA : register(c21);  // rgb cloud albedo a, w powder factor (sigma * 2 * length)
float4 PuffColourB : register(c22);  // rgb cloud albedo b, w 1 / the cloud near fade width
float4 Ambient : register(c23);      // rgb ambient light, w occlusion factor (sigma * dist * k)
float4 AoStep : register(c24);       // xyz occlusion probe offset (noise space, m)
float4 KeyDir : register(c25);       // xyz toward the key light, w forward phase g
float4 KeyColour : register(c26);    // rgb key light (0 = none), w backward phase g
float4 KeyDirNoise : register(c27);  // xyz key direction in noise space, w backward lobe weight
float4 Scatter : register(c28);      // x multiple-scattering strength, y extinction, z phase,
                                     // w extinction^2 (precomputed: see c19.w)
float4 ShadowStep[NEBULA_SHADOW_STEPS] : register(c29); // x distance along the key (m), y optical
                                     // weight (sigma * density * length; 0 = step unused)
float4 Misc : register(c35);         // x exclusion count, y point light count, z in-shader
                                     // tonemap mode (8-bit target only), w its exposure
float4 TargetSize : register(c36);   // xy this target's size in pixels, z tonemap white
// Exclusion zones (up to 8, nearest first): centre + 1 / edge fraction, then the three local
// rows; row 0's w is the haze density scale inside the zone minus 1, row 1's w the zone's
// bounding radius (m). (Reciprocal and offset precomputed: see c19.w.)
float4 ExclusionCentre[NEBULA_MAX_EXCLUSIONS] : register(c37);
float4 ExclusionRow0[NEBULA_MAX_EXCLUSIONS] : register(c45);
float4 ExclusionRow1[NEBULA_MAX_EXCLUSIONS] : register(c53);
float4 ExclusionRow2[NEBULA_MAX_EXCLUSIONS] : register(c61);
// Point lights (lightning, count in Misc.y, nearest first; unused ones are zero):
// LightPos xyz camera-relative position, w the base cloud amount (0..1) at the strike;
// LightColour rgb the light's radiance at its core (colour x peak x flicker envelope), w the
// squared distance camera - light (precomputed: see c19.w).
float4 LightPos[NEBULA_MAX_POINT_LIGHTS] : register(c69);
float4 LightColour[NEBULA_MAX_POINT_LIGHTS] : register(c73);
float4 Lod : register(c77);          // x LOD start (m to the zone entry), y 1 / LOD range,
                                     // z step fraction at full LOD, w LOD of the shadow switch
float4 Exterior : register(c78);     // x volume strength (exterior fade), y pixel angle (rad),
                                     // z 1 / detail texel (m; 0 = no detail fade), w 1 = ignore
                                     // the scene depth (drawn before the opaque scene)
float4 Budget : register(c79);       // x instructions per step, y per exclusion and step, z the
                                     // executed-instruction budget, w 1 / haze shadow fade-in (m)
float4 MacroShadow : register(c80);  // x haze extinction of the large-scale shadow (1/m)
float4 ShadowStepHalf[NEBULA_SHADOW_STEPS_HALF] : register(c81); // pair-merged shadow steps
float4 LightShape : register(c84);   // x soft core radius^2 (m^2), y 1 / light radius^2,
                                     // z 1 - g^2 (phase), w extinction factor light - sample
float4 LightMore : register(c85);    // x 1 / emissive core radius^2, y haze scale, z emissive
                                     // core strength, w 1 = one midpoint cloud sample per light
float4 LightExtra : register(c86);   // x 1 = cores ignore the medium (debug positions),
                                     // y light radius^2 (m^2), z 1 + g^2, w 2 g (phase)
float4 PuffColourDelta : register(c87); // rgb cloud albedo b - a (same reason as c19.w)
// The outer shell and the halo (NebulaShellProfile / NebulaHalo, all in the zone's unit space):
float4 Shell : register(c88);        // x 1 / shell width, y displacement amplitude, z the radius
                                     // where the displacement can start to matter (1 - width -
                                     // amplitude), w the coverage rise at the surface
float4 Halo : register(c89);         // x halo end (1 + halo_extent), y 1 / halo_extent, z halo
                                     // density, w the ray span's radius^2 ((end + amplitude)^2)
float4 ShellNoise : register(c90);   // xyz displacement noise offset (tile), w its frequency
                                     // (tiles per unit of normalised radius)
float4 ShellMore : register(c91);    // x exterior_shell_density - 1 times the camera's exterior
                                     // weight, y coverage softening and z detail fade at full
                                     // LOD, w the middle of the coverage ramp (Shape.x + 0.5 /
                                     // Shape.y; precomputed: see c19.w)
float4 KeyLocal : register(c92);     // xyz the key direction in the zone's unit space, w its
                                     // squared length (precomputed: see c19.w)
// Objects the scene depth does not hold (NebulaVolume::Note*: what the frame drew before this
// pass). Opaque spheres -- planets and moons, which the far bucket draws without a depth test --
// end the ray where it hits them: xyz centre (camera-relative), w |centre|^2 - radius^2 (1e30 =
// unused). Star glows (additive sprites drawn without depth): xyz centre, w 1 / glow radius (m);
// StarDistance xyz their distances (-1 unused); StarWeight xyz how much of the scene the glow is
// at its centre (sprite alpha x star_glow_weight; 0 unused). Inside a glow the scene is
// attenuated by the transmittance up to the star, not by the whole ray's.
float4 FarSphere[4] : register(c93);
float4 StarGlow[3] : register(c97);
float4 StarDistance : register(c100);
float4 StarWeight : register(c101);
// Asteroid cubes fading out at the end of the extended spawn distance (CliAsteroidDrawNearField)
// write depth while translucent: x the view depth where their alpha reaches 0, y 1 / the fade
// band, z the view depth up to which a pixel can belong to such a cube (0 = none).
float4 AsteroidFade : register(c102);

FFullscreenVertex VSMain(float3 Position : POSITION, float2 TexCoord : TEXCOORD0)
{
    return RPExFullscreenVS(Position, TexCoord);
}

// World (camera-relative) -> noise space: the zone's up axis compressed by the layer stretch,
// so the clouds flatten into decks.
float3 NebulaToNoise(float3 P)
{
    return P + NoiseSpace.xyz * (dot(P, NoiseSpace.xyz) * NoiseSpace.w);
}

// Base-shape cloud amount (no erosion) at a noise-space point: shadow rays and occlusion.
float NebulaBaseCloud(float3 Pn)
{
    float Base = tex3Dlod(Noise, float4(Pn * BaseNoise.w + BaseNoise.xyz, 0.0)).r;
    return NebulaCloudFromBase(Base, Shape.xy);
}

// One exclusion zone (centre + 1 / edge fraction, the three local rows, row 0's w the haze
// density scale inside it minus 1): the haze thins, the clouds go.
void NebulaExclusionZone(float3 P, float4 Centre, float4 Row0, float3 Row1, float3 Row2,
                         inout float Thin, inout float KeepClouds)
{
    float3 L = NebulaZoneLocal(P, Centre.xyz, Row0.xyz, Row1, Row2);
    float E = NebulaExclusionImmersion(L, Centre.w);
    Thin *= 1.0 + Row0.w * E;
    KeepClouds *= 1.0 - E;
}

// The medium at a camera-relative point P, T metres along the ray. Haze: 0..1 of the haze
// density; Cloud: 0..1 of the cloud density; ColourNoise: puff colour a/b blend; Warped: the
// warped noise-space point (the light probes start there); Local: P in the zone's unit space.
// ExRange: the ray's span through the exclusion zones' bounding spheres (x..y) and the number
// of zones to test there (z). DetailFade: 1 = full edge detail, 0 = its mean (far away).
// LodF: the level of detail (0..1); far away the coverage ramp widens about its middle
// (ShellMore.y): soft masses instead of hard-edged blotches.
void NebulaMedium(float3 P, float T, float3 ExRange, float DetailFade, float LodF,
                  out float Haze, out float Cloud, out float ColourNoise, out float3 Warped,
                  out float3 Local)
{
    Haze = 0.0;
    Cloud = 0.0;
    ColourNoise = 0.5;
    Warped = float3(0.0, 0.0, 0.0);

    // The nebula zone: a core, a wide outer shell whose radius is displaced by low-frequency
    // noise (an irregular silhouette, big lobes) and over which the medium thins out, and a thin
    // haze halo beyond the surface. Inside the radius where the displacement cannot reach the
    // shell the profile is 1 and the noise lookup is skipped. Nothing beyond the halo's end
    // (Halo.x; the CPU sets it to 1 without a halo). Only the radius stays live through the
    // exclusion test (temp registers), the profile is evaluated after it.
    Local = NebulaZoneLocal(P, ZoneCentre.xyz, ZoneRow[0].xyz, ZoneRow[1].xyz, ZoneRow[2].xyz);
    float Qd = length(Local);
    [branch] if (Qd > Shell.z)
        Qd += Shell.y * (2.0 * tex3Dlod(Noise, float4(Local * ShellNoise.w + ShellNoise.xyz,
                                                       0.0)).a - 1.0);
    if (Qd >= Halo.x)
        return;

    // Exclusion zones: the clouds go, the haze thins to the zone's fog_far visibility. Only
    // where the ray runs through one of their bounding spheres.
    float Thin = 1.0;
    float KeepClouds = 1.0;
    // Unrolled (no dynamic array index: ps_3_0 has no relative constant addressing in pixel
    // shaders, a loop would select each array element with a cmp chain -- slower and short of
    // temp registers), zone 0 always, the rest only up to the last zone the ray crosses.
    if (T >= ExRange.x && T <= ExRange.y)
    {
        NebulaExclusionZone(P, ExclusionCentre[0], ExclusionRow0[0], ExclusionRow1[0].xyz,
                            ExclusionRow2[0].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 1.5)
            NebulaExclusionZone(P, ExclusionCentre[1], ExclusionRow0[1], ExclusionRow1[1].xyz,
                                ExclusionRow2[1].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 2.5)
            NebulaExclusionZone(P, ExclusionCentre[2], ExclusionRow0[2], ExclusionRow1[2].xyz,
                                ExclusionRow2[2].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 3.5)
            NebulaExclusionZone(P, ExclusionCentre[3], ExclusionRow0[3], ExclusionRow1[3].xyz,
                                ExclusionRow2[3].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 4.5)
            NebulaExclusionZone(P, ExclusionCentre[4], ExclusionRow0[4], ExclusionRow1[4].xyz,
                                ExclusionRow2[4].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 5.5)
            NebulaExclusionZone(P, ExclusionCentre[5], ExclusionRow0[5], ExclusionRow1[5].xyz,
                                ExclusionRow2[5].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 6.5)
            NebulaExclusionZone(P, ExclusionCentre[6], ExclusionRow0[6], ExclusionRow1[6].xyz,
                                ExclusionRow2[6].xyz, Thin, KeepClouds);
        [branch] if (ExRange.z > 7.5)
            NebulaExclusionZone(P, ExclusionCentre[7], ExclusionRow0[7], ExclusionRow1[7].xyz,
                                ExclusionRow2[7].xyz, Thin, KeepClouds);
    }

    // The clear radius around the camera ([Fog] near).
    float ClearT = saturate(T * HazeEmission.w);
    float Clear = HazeEmission.w > 0.0 ? ClearT * ClearT * (3.0 - 2.0 * ClearT) : 1.0;
    float Outer;
    float Profile = NebulaShellProfile(Qd, Shell.x, ShellMore.x, Outer);
    Haze = max(Profile, NebulaHalo(Qd, Halo.x, Halo.y, Halo.z)) * Thin * Clear;
    // The halo holds no clouds: no warp / base lookups (the light probes start unwarped).
    float3 Pn = NebulaToNoise(P);
    Warped = Pn;
    if (Profile <= 0.0)
        return;

    // Domain warp (wisps, swirls), then the base shape cut by the coverage threshold. Across
    // the shell the threshold rises toward 1 (Outer^2 * Shell.w of the way: sparse puffs and
    // wisps near the surface, none beyond it), and far away the ramp widens about its middle
    // (LOD 0 = exactly NebulaCloudFromBase).
    float3 Warp = tex3Dlod(Noise, float4(Pn * WarpNoise.w + WarpNoise.xyz, 0.0)).rgb - 0.5;
    Warped = Pn + Warp * ShapeMore.x;
    float4 Base = tex3Dlod(Noise, float4(Warped * BaseNoise.w + BaseNoise.xyz, 0.0));
    ColourNoise = Base.a;
    float Rise = Outer * Outer * Shell.w * (1.0 - Shape.x);
    float C = saturate((Base.r - ShellMore.w - Rise) * (Shape.y / (1.0 + LodF * ShellMore.y)) + 0.5);
    if (C > 0.0)
    {
        // Only inside a cloud: the detail lookup that erodes its edges (far away blended
        // toward the detail's mean, so the edges do not shimmer once a texel is under a pixel).
        float2 Detail = float2(0.5, 0.5);
        if (DetailFade > 0.0)
            Detail = lerp(Detail, tex3Dlod(Noise, float4(Warped * DetailNoise.w + DetailNoise.xyz,
                                                          0.0)).gb, DetailFade);
        C = NebulaErode(C, Detail, Shape.z, Shape.w);
        // The clouds fade in over the [Clouds] near_fade_distance range, like retail's puffs.
        float NearFade = saturate((T - ShapeMore.y) * PuffColourB.w);
        Cloud = C * Profile * KeepClouds * NearFade * Clear;
    }
}

// The nearest hit of the ray on the opaque far spheres (FarSphere), 1e30 for none (a camera inside
// a sphere does not count).
float NebulaFarSphereHit(float3 Dir)
{
    float4 B = float4(dot(Dir, FarSphere[0].xyz), dot(Dir, FarSphere[1].xyz),
                      dot(Dir, FarSphere[2].xyz), dot(Dir, FarSphere[3].xyz));
    float4 Disc = B * B - float4(FarSphere[0].w, FarSphere[1].w, FarSphere[2].w, FarSphere[3].w);
    float4 Hit = B - sqrt(max(Disc, 0.0));
    Hit = (Disc >= 0.0) * (Hit > 0.0) > 0.0 ? Hit : 1.0e30;
    return min(min(Hit.x, Hit.y), min(Hit.z, Hit.w));
}

// How much of a scene pixel at view depth ViewZ is an asteroid fading out (AsteroidFade): its
// alpha there, 1 for everything that is not in the fade band.
float NebulaAsteroidKeep(float ViewZ)
{
    return ViewZ < AsteroidFade.z ? saturate((AsteroidFade.x - ViewZ) * AsteroidFade.y) : 1.0;
}

// The mean density of the shell / halo profile (without the displacement) a third and two thirds
// along [T0, T0 + Length] of the ray: the uniform haze integrated in closed form beyond the march.
float NebulaTailDensity(float3 Dir, float T0, float Length)
{
    float Outer;
    float Q1 = length(NebulaZoneLocal(Dir * (T0 + Length * 0.3333), ZoneCentre.xyz, ZoneRow[0].xyz,
                                      ZoneRow[1].xyz, ZoneRow[2].xyz));
    float Q2 = length(NebulaZoneLocal(Dir * (T0 + Length * 0.6667), ZoneCentre.xyz, ZoneRow[0].xyz,
                                      ZoneRow[1].xyz, ZoneRow[2].xyz));
    return 0.5 * (max(NebulaShellProfile(Q1, Shell.x, ShellMore.x, Outer),
                      NebulaHalo(Q1, Halo.x, Halo.y, Halo.z)) +
                  max(NebulaShellProfile(Q2, Shell.x, ShellMore.x, Outer),
                      NebulaHalo(Q2, Halo.x, Halo.y, Halo.z)));
}

#if NEBULA_POINT_LIGHTS
// A strike's emissive core: a Gaussian blob (LightMore.x = 1 / its radius^2) integrated across
// the ray, i.e. by the ray's closest approach to the light; LightMore.z its strength.
// PosSquared: the light's squared distance from the camera; LT: its distance along the ray.
float NebulaCoreGlow(float PosSquared, float LT)
{
    return exp(-max(PosSquared - LT * LT, 0.0) * LightMore.x) * LightMore.z;
}

// One lightning point light at the sample P = Dir * T. PointIn += the radiance it scatters
// here, before albedo: its core radiance x falloff (inverse square with a soft core,
// LightShape.x = core^2, windowed to 0 at the light radius, 1 / radius^2 in LightShape.y) x a
// near-isotropic Henyey-Greenstein phase (LightShape.z = 1 - g^2, LightExtra.z = 1 + g^2,
// LightExtra.w = 2 g) x the medium between light and sample, an optical depth estimate -- the
// haze plus the mean of the cloud here and at the strike (PosCloud.w), with bMidSample one
// base-shape sample halfway instead -- times the distance, scaled by LightShape.w, through the
// diffusion falloff 1 / (1 + tau) (multiple scattering carries the light much farther through
// a cloud than a shadow ray would).
// CoreIn += its emissive core when the light lies in this step's segment [SegStart, SegEnd).
// Everything is derived from ToLight (which changes per step): terms that depend only on the
// light and the ray would be hoisted out of the march by the compiler, one temp register each.
void NebulaPointLight(float4 PosCloud, float4 Colour, float3 P, float T, float3 Dir, float Cloud,
                      float SigmaH, float3 Warped, bool bMidSample, float SegStart, float SegEnd,
                      inout float3 PointIn, inout float3 CoreIn)
{
    float3 ToLight = PosCloud.xyz - P;
    float D2 = dot(ToLight, ToLight);
    float Window = saturate(1.0 - D2 * LightShape.y);
    [branch] if (Window > 0.0)
    {
        float InvD = rsqrt(max(D2, 1.0));
        float Along = dot(Dir, ToLight);
        float Fall = LightShape.x / (D2 + LightShape.x) * Window * Window;
        float PhaseDenom = max(LightExtra.z - LightExtra.w * (Along * InvD), 1.0e-4);
        float PointPhase = LightShape.z / (PhaseDenom * sqrt(PhaseDenom));
        float Between = 0.5 * (Cloud + PosCloud.w);
        [branch] if (bMidSample)
            Between = 0.25 * (Cloud + PosCloud.w) +
                      0.5 * NebulaBaseCloud(Warped + NebulaToNoise(ToLight) * 0.5);
        // Diffuse transport, not a shadow ray: light that multiply scatters through a dense
        // cloud falls off like 1 / (1 + tau) (diffusion through a conservative slab), not
        // exp(-tau) -- with cloud_density 12 exp would leave nothing a cloud cell away.
        float Tau = (SigmaH + Medium.y * Between) * (D2 * InvD) * LightShape.w;
        PointIn += Colour.rgb * (Fall * PointPhase / (1.0 + Tau));
        // The light's distance along the ray is T + Along, the ray's closest approach to it
        // D2 - Along^2.
        float LT = T + Along;
        [branch] if (LT >= SegStart && LT < SegEnd)
            CoreIn += Colour.rgb *
                      (exp(-max(D2 - Along * Along, 0.0) * LightMore.x) * LightMore.z);
    }
}

// The emissive core of a light beyond the marched part (TMarched) and in front of the scene /
// the zone exit (TFar).
void NebulaCoreBeyond(float4 PosCloud, float4 Colour, float3 Dir, float TMarched, float TFar,
                      inout float3 CoreIn)
{
    float LT = dot(Dir, PosCloud.xyz);
    [branch] if (LT >= TMarched && LT < TFar)
        CoreIn += Colour.rgb * NebulaCoreGlow(Colour.w, LT);
}
#endif

float4 PSRaymarch(float2 TexCoord : TEXCOORD0) : COLOR0
{
    // Outside the camera's viewport: nothing.
    float2 ViewUv = (TexCoord - ViewRect.xy) / ViewRect.zw;
    if (any(ViewUv < 0.0) || any(ViewUv > 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);

    // The scene depth of the full-resolution texel this pixel stands for (the composite reads
    // the same one).
    float2 DepthUv = TexCoord + ProjInv.zw;
    float Device = tex2Dlod(SceneDepth, float4(DepthUv, 0.0, 0.0)).r;

    // View ray: D3D view space (x right, y up, z forward) at z = 1, then into world axes
    // (the game's view looks down -z: world = x * Row0 + y * Row1 - Row2).
    float2 Ndc = float2(ViewUv.x * 2.0 - 1.0, 1.0 - ViewUv.y * 2.0);
    float2 ViewXY = Ndc * ProjInv.xy;
    float3 Dir = ViewXY.x * CamRow[0].xyz + ViewXY.y * CamRow[1].xyz - CamRow[2].xyz;
    float DirLength = length(Dir);
    Dir /= DirLength;

    // Where the ray runs: from the zone entry (the camera when inside) to the scene (sky =
    // depth 1.0 = unlimited; everything before the opaque scene: Exterior.w), the zone exit
    // and the march cap counted from the entry. "The zone" here is the halo's outer bound
    // (Halo.w: the displaced halo end), so the march covers the shell and the halo.
    // Opaque spheres drawn without depth (far planets) end the ray as well.
    bool bSceneDepth = Device < 1.0 && Exterior.w < 0.5;
    float SceneT = bSceneDepth ? RPExLinearDepth(Device, ProjZ) * DirLength : 1.0e30;
    SceneT = min(SceneT, NebulaFarSphereHit(Dir));
    float2 Span = NebulaZoneSpan(Dir, ZoneCentre.xyz, ZoneRow[0].xyz, ZoneRow[1].xyz,
                                 ZoneRow[2].xyz, Halo.w);
    float TStart = max(Span.x, 0.0);
    if (Span.y <= TStart)
        return float4(0.0, 0.0, 0.0, 1.0);
    float TFar = min(Span.y, SceneT);
    float TEnd = min(TFar, TStart + March.y);
    if (TEnd <= TStart)
    {
        // The scene is in front of the zone -- unless it is an asteroid fading out: the medium
        // behind it is added after the (empty) march, as far as the asteroid lets it through.
        if (!bSceneDepth || NebulaAsteroidKeep(RPExLinearDepth(Device, ProjZ)) >= 1.0)
            return float4(0.0, 0.0, 0.0, 1.0);
        TFar = TStart;
        TEnd = TStart;
    }

    float Jitter = NebulaInterleavedGradient(TexCoord * TargetSize.xy) * March.w + 0.5 * (1.0 - March.w);

    // Level of detail by the distance to the zone entry (0 from inside).
    float LodF = saturate((TStart - Lod.x) * Lod.y);
    bool bHalfShadow = LodF >= Lod.w;
    // The large-scale haze shadow toward the key light, faded in with the entry distance (0
    // from inside, where the owner-tuned look has none).
    float MacroWeight = saturate(TStart * Budget.w);

    // The ray's span through the exclusion zones' bounding spheres (nearest first): the
    // exclusion loop runs only there and only over the zones up to the last one hit.
    float3 ExRange = float3(1.0e30, -1.0e30, 0.0);
    [loop] for (int Ex = 0; Ex < NEBULA_MAX_EXCLUSIONS; ++Ex)
    {
        if (Ex >= Misc.x)
            break;
        float3 C = ExclusionCentre[Ex].xyz;
        float R = ExclusionRow1[Ex].w;
        float B = dot(Dir, C);
        float Disc = B * B - (dot(C, C) - R * R);
        if (Disc >= 0.0)
        {
            float Root = sqrt(Disc);
            ExRange.x = min(ExRange.x, B - Root);
            ExRange.y = max(ExRange.y, B + Root);
            ExRange.z = Ex + 1.0;
        }
    }

#if NEBULA_POINT_LIGHTS
    // Point lights: the ray's span through the light spheres (unused lights sit at the origin
    // with no colour and are not counted); the per-step light work runs only there.
    float4 LightT = float4(dot(Dir, LightPos[0].xyz), dot(Dir, LightPos[1].xyz),
                           dot(Dir, LightPos[2].xyz), dot(Dir, LightPos[3].xyz));
    float4 LightCC = float4(LightColour[0].w, LightColour[1].w, LightColour[2].w, LightColour[3].w);
    float4 LightDisc = LightT * LightT - (LightCC - LightExtra.y);
    float4 LightValid = (LightDisc >= 0.0) * (float4(0.0, 1.0, 2.0, 3.0) < Misc.y);
    float4 LightRoot = sqrt(max(LightDisc, 0.0));
    float4 LightLo = LightValid > 0.0 ? LightT - LightRoot : 1.0e30;
    float4 LightHi = LightValid > 0.0 ? LightT + LightRoot : -1.0e30;
    float2 LRange = float2(min(min(LightLo.x, LightLo.y), min(LightLo.z, LightLo.w)),
                           max(max(LightHi.x, LightHi.y), max(LightHi.z, LightHi.w)));
#endif

    // Step count: the LOD fraction, then capped so the executed instructions stay under the
    // device's limit with the exclusion zones this ray has to test (the per-light cost is in
    // Budget.x).
    float Steps = floor(March.x * lerp(1.0, Lod.z, LodF) + 0.5);
    Steps = max(min(Steps, floor(Budget.z / (Budget.x + Budget.y * ExRange.z))), 1.0);
    if (TEnd <= TStart)
        Steps = 0.0;

    // Per-pixel light terms (the angle to the key light is the same along the whole ray): the
    // single-scattering phase and two multiple-scattering octaves (weaker, wider).
    float CosKey = dot(Dir, KeyDir.xyz);
    float G1 = Scatter.z;
    float G2 = Scatter.z * Scatter.z;
    float Phase0 = NebulaPhaseDual(CosKey, KeyDir.w, KeyColour.w, KeyDirNoise.w);
    float Phase1 = Scatter.x * NebulaPhaseDual(CosKey, KeyDir.w * G1, KeyColour.w * G1, KeyDirNoise.w);
    float Phase2 = Scatter.x * Scatter.x *
                   NebulaPhaseDual(CosKey, KeyDir.w * G2, KeyColour.w * G2, KeyDirNoise.w);
    float Ext1 = Scatter.y;
    float Ext2 = Scatter.w;
    // Powder (dark, unlit-looking edges) mostly when looking away from the light.
    float PowderMix = CloudEmission.w * saturate(0.5 - 0.5 * CosKey);
    bool bKey = dot(KeyColour.rgb, float3(1.0, 1.0, 1.0)) > 0.0;

    float3 Light = float3(0.0, 0.0, 0.0);
    float Transmittance = 1.0;
    float ShadowSum = 0.0;
    float ShadowCount = 0.0;
    // Debug views 1 and 2 accumulate in Light instead (fewer live registers): x the clouds'
    // optical depth along the ray, y the visible-weighted shadow, z its weight.
    float PrevT = TStart;
    float SpanLength = TEnd - TStart;
    // [fastopt]: without it the June 2010 HLSL compiler (d3dx9_43, what RP9 compiles with)
    // spends minutes on this loop.
    [fastopt] [loop] for (int Step = 0; Step < NEBULA_MAX_STEPS; ++Step)
    {
        if (Step >= Steps || Transmittance < 0.01)
            break;
        // Steps crowd toward the entry (distribution > 1); the sample sits inside its
        // segment at the jittered fraction.
        float NextT = TStart + SpanLength * pow(saturate((Step + 1.0) / Steps), March.z);
        float Dt = NextT - PrevT;
        float T = lerp(PrevT, NextT, Jitter);
        PrevT = NextT;
        float3 P = Dir * T;

        // Edge detail fade (LOD only): once a detail texel is smaller than the pixel footprint
        // or half the step; and with the LOD toward the base shape (ShellMore.z).
        float DetailFade = (1.0 - LodF * (1.0 - saturate(2.0 - max(T * Exterior.y, 0.5 * Dt) * Exterior.z))) *
                           (1.0 - LodF * ShellMore.z);

        float Haze;
        float Cloud;
        float ColourNoise;
        float3 Warped;
        float3 Local;
        NebulaMedium(P, T, ExRange, DetailFade, LodF, Haze, Cloud, ColourNoise, Warped, Local);
        float SigmaH = Medium.x * Haze;
        float SigmaC = Medium.y * Cloud;
        float Sigma = SigmaH + SigmaC;

        if (Sigma > 0.0)
        {
#if NEBULA_POINT_LIGHTS
            // Lightning (NebulaPointLight): the lights' in-scattered radiance, weighted by the
            // haze / cloud albedo like the rest (added to Radiance below), and the emissive
            // cores of lights inside this step's segment, seen through the medium up to it.
            // Before the key-light part, so only PointRadiance stays live through it (ps_3_0
            // has 32 temp registers).
            float3 PointRadiance = float3(0.0, 0.0, 0.0);
            [branch] if (T >= LRange.x && T <= LRange.y)
            {
                float3 PointIn = float3(0.0, 0.0, 0.0);
                float3 CoreIn = float3(0.0, 0.0, 0.0);
                float SegStart = PrevT - Dt;
                bool bMid = LightMore.w > 0.5 && !bHalfShadow;
                NebulaPointLight(LightPos[0], LightColour[0], P, T, Dir, Cloud, SigmaH, Warped,
                                 bMid, SegStart, PrevT, PointIn, CoreIn);
                [branch] if (Misc.y > 1.5)
                {
                    NebulaPointLight(LightPos[1], LightColour[1], P, T, Dir, Cloud, SigmaH,
                                     Warped, bMid, SegStart, PrevT, PointIn, CoreIn);
                    [branch] if (Misc.y > 2.5)
                    {
                        NebulaPointLight(LightPos[2], LightColour[2], P, T, Dir, Cloud, SigmaH,
                                         Warped, bMid, SegStart, PrevT, PointIn, CoreIn);
                        [branch] if (Misc.y > 3.5)
                            NebulaPointLight(LightPos[3], LightColour[3], P, T, Dir, Cloud,
                                             SigmaH, Warped, bMid, SegStart, PrevT, PointIn,
                                             CoreIn);
                    }
                }
                float3 LightAlbedo = PuffColourA.rgb + PuffColourDelta.rgb * ColourNoise;
                PointRadiance = PointIn * ((SigmaH * LightMore.y) * HazeAlbedo.rgb +
                                           SigmaC * LightAlbedo) / Sigma;
                Light += CoreIn * lerp(Transmittance, 1.0, LightExtra.x);
            }
#endif

            // Key-light shadow: optical depth of the base-shape clouds toward the light, in
            // exponentially growing steps (the weights carry sigma, density and length); far
            // away the pair-merged half set.
            float Tau = 0.0;
            if (bKey)
            {
                if (bHalfShadow)
                {
                    [loop] for (int Half = 0; Half < NEBULA_SHADOW_STEPS_HALF; ++Half)
                    {
                        float4 HalfSample = ShadowStepHalf[Half];
                        Tau += NebulaBaseCloud(Warped + KeyDirNoise.xyz * HalfSample.x) *
                               HalfSample.y;
                    }
                }
                else
                {
                    [loop] for (int Shadow = 0; Shadow < NEBULA_SHADOW_STEPS; ++Shadow)
                    {
                        float4 ShadowSample = ShadowStep[Shadow];
                        Tau += NebulaBaseCloud(Warped + KeyDirNoise.xyz * ShadowSample.x) *
                               ShadowSample.y;
                    }
                }
                // From outside: the haze between the sample and the zone surface toward the
                // light (the sun side lit, the far side darker); none in the halo (outside the
                // surface the exit would be meaningless).
                [branch] if (MacroWeight > 0.0)
                {
                    float LB = dot(Local, KeyLocal.xyz);
                    float LC = dot(Local, Local) - 1.0;
                    float Exit = (-LB + sqrt(max(LB * LB - KeyLocal.w * LC, 0.0))) / KeyLocal.w;
                    Tau += MacroShadow.x * (LC < 0.0 ? max(Exit, 0.0) : 0.0) * MacroWeight;
                }
            }
            float ShadowT = exp(-Tau);
            ShadowSum += ShadowT;
            ShadowCount += 1.0;
            // Ambient occlusion: the cloud one probe step up the zone's axis (dark undersides).
            float Occlusion = exp(-NebulaBaseCloud(Warped + AoStep.xyz) * Ambient.w);

            float3 Key = KeyColour.rgb;
            float KeyTerm = Phase0 * ShadowT + Phase1 * exp(-Tau * Ext1) + Phase2 * exp(-Tau * Ext2);
            float Powder = lerp(1.0, 1.0 - exp(-Cloud * PuffColourA.w), PowderMix);
            float3 Albedo = PuffColourA.rgb + PuffColourDelta.rgb * ColourNoise;
            float3 CloudRadiance = Albedo * (Key * (KeyTerm * Powder) + Ambient.rgb * Occlusion) +
                                   CloudEmission.rgb;
            float3 HazeRadiance = HazeAlbedo.rgb * (Key * (Phase0 * ShadowT) + Ambient.rgb * Occlusion) +
                                  HazeEmission.rgb;
            float3 Radiance = (SigmaH * HazeRadiance + SigmaC * CloudRadiance) / Sigma;
#if NEBULA_POINT_LIGHTS
            Radiance += PointRadiance;
#endif

            // Energy-conserving step: the fraction absorbed here is replaced by the radiance.
            float Alpha = 1.0 - exp(-Sigma * Dt);
            [branch] if (Medium.w > 0.5)
                Light += float3(SigmaC * Dt, Transmittance * Alpha * ShadowT, Transmittance * Alpha);
            else
                Light += Transmittance * Alpha * Radiance;
            Transmittance *= 1.0 - Alpha;
        }
    }

    if (Medium.w > 1.5)
        return float4((Light.z > 0.0 ? Light.y / Light.z : 1.0).xxx, 0.0);
    if (Medium.w > 0.5)
        return float4((1.0 - exp(-Light.x)).xxx, 0.0);

#if NEBULA_POINT_LIGHTS
    // Emissive cores of the lights beyond the marched part (past the march cap or an early
    // exit), in front of the scene, seen through what was marched.
    {
        float3 CoreIn = float3(0.0, 0.0, 0.0);
        NebulaCoreBeyond(LightPos[0], LightColour[0], Dir, PrevT, TFar, CoreIn);
        NebulaCoreBeyond(LightPos[1], LightColour[1], Dir, PrevT, TFar, CoreIn);
        NebulaCoreBeyond(LightPos[2], LightColour[2], Dir, PrevT, TFar, CoreIn);
        NebulaCoreBeyond(LightPos[3], LightColour[3], Dir, PrevT, TFar, CoreIn);
        Light += CoreIn * lerp(Transmittance, 1.0, LightExtra.x);
    }
#endif

    // Beyond the march cap the haze goes on to the halo's end (or the scene): integrated in
    // closed form as uniform haze, its density the mean of the shell / halo profile (without
    // the displacement) a third and two thirds along the rest -- a ray through the thin outer
    // layers stays thin -- lit with the mean shadow of the marched part.
    float MeanShadow = ShadowCount > 0.0 ? ShadowSum / ShadowCount : 1.0;
    float3 TailRadiance = HazeAlbedo.rgb * (KeyColour.rgb * (Phase0 * MeanShadow) + Ambient.rgb) +
                          HazeEmission.rgb;
    float TLoop = Transmittance;
    float TailSigma = 0.0;
    if (ShapeMore.w > 0.5 && TFar > TEnd && Transmittance >= 0.01)
    {
        float TailLength = min(TFar - TEnd, 1.0e7);
        TailSigma = Medium.x * NebulaTailDensity(Dir, TEnd, TailLength);
        float TailAlpha = 1.0 - exp(-TailSigma * TailLength);
        Light += Transmittance * TailAlpha * TailRadiance;
        Transmittance *= 1.0 - TailAlpha;
    }

    // The transmittance up to each star (StarDistance): 1 in front of the zone entry; inside the
    // marched part an estimate with the extinction spread evenly over it (recording it per step
    // would need a temp register the march does not have); beyond it the tail's closed form (all
    // of it when the star lies beyond the scene or the zone exit).
    float3 TProbe = exp(log(max(TLoop, 1.0e-6)) *
                        saturate((StarDistance.xyz - TStart) / max(PrevT - TStart, 1.0e-3)));
    TProbe = StarDistance.xyz > PrevT
                 ? TLoop * exp(-TailSigma * clamp(StarDistance.xyz - TEnd, 0.0, max(TFar - TEnd, 0.0)))
                 : TProbe;

    // An asteroid fading out at the end of the extended spawn distance wrote its depth with a
    // fraction Keep (its alpha) of its colour: the march stopped there, but 1 - Keep of the pixel
    // is the scene behind it. The haze behind it, out to the zone exit, is added for that part
    // (in closed form like the tail), so a fading asteroid does not cut its silhouette out of the
    // nebula behind it.
    if (bSceneDepth)
    {
        float Keep = NebulaAsteroidKeep(RPExLinearDepth(tex2Dlod(SceneDepth, float4(TexCoord + ProjInv.zw,
                                                                                   0.0, 0.0)).r,
                                                        ProjZ));
        float2 SpanAll = NebulaZoneSpan(Dir, ZoneCentre.xyz, ZoneRow[0].xyz, ZoneRow[1].xyz,
                                        ZoneRow[2].xyz, Halo.w);
        float BehindStart = max(TFar, TStart);
        float BehindLength = min(SpanAll.y - BehindStart, 1.0e7);
        if (Keep < 1.0 && BehindLength > 0.0)
        {
            float BehindAlpha = (1.0 - exp(-Medium.x * BehindLength *
                                           NebulaTailDensity(Dir, BehindStart, BehindLength))) *
                                (1.0 - Keep);
            Light += Transmittance * BehindAlpha * TailRadiance;
            Transmittance *= 1.0 - BehindAlpha;
        }
    }

    // Star glows (additive, no depth): where a glow is most of the scene the pixel takes the
    // transmittance up to the star instead of the whole ray's, so a star in front of (or inside)
    // the nebula stays visible. W: the glow's weight, falling from StarWeight at its centre to 0
    // at its radius (the offset measured in the sprite's plane).
    {
        float3 Along = float3(dot(Dir, StarGlow[0].xyz), dot(Dir, StarGlow[1].xyz),
                              dot(Dir, StarGlow[2].xyz));
        float3 Dist = StarDistance.xyz;
        float3 Offset = sqrt(max(Dist * Dist - Along * Along, 0.0)) * Dist / max(Along, 1.0e-3 * Dist);
        float3 W = Along > 0.0
                       ? saturate((1.0 - Offset * float3(StarGlow[0].w, StarGlow[1].w, StarGlow[2].w)) *
                                  StarWeight.xyz)
                       : 0.0;
        float3 TStar = lerp(Transmittance.xxx, max(TProbe, Transmittance), W);
        Transmittance = max(max(TStar.x, TStar.y), TStar.z);
    }

    // Exterior fade-out toward exterior_max_distance (1 when inside or near).
    if (Exterior.x < 1.0)
    {
        Light *= Exterior.x;
        Transmittance = lerp(1.0, Transmittance, Exterior.x);
    }

    // 8-bit target: tonemap here (the composite then passes it through).
    if (Misc.z > 0.5)
        Light = RPExTonemap(Light * Misc.w, Misc.z, TargetSize.z);
    return float4(Light, Transmittance);
}
