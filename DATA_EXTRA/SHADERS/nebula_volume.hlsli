// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Shared helpers of the volumetric nebula (port-only; freelancer.exe Client/nebula_volume.cpp,
// RP9.dll ps_3_0). Pure functions: every tunable value arrives as a parameter, the constants
// themselves are declared by the shaders that include this. See docs/re/rp9.md, "Volumetric
// nebula".

#ifndef NEBULA_VOLUME_HLSLI
#define NEBULA_VOLUME_HLSLI

#include "common.hlsli"

// Interleaved gradient noise (Jimenez 2014): a per-pixel 0..1 offset with little visible
// structure, used to jitter the ray-march samples.
float NebulaInterleavedGradient(float2 Pixel)
{
    return frac(52.9829189 * frac(dot(Pixel, float2(0.06711056, 0.00583715))));
}

// Henyey-Greenstein phase times 4 pi (isotropic scattering = 1). CosTheta: the cosine between
// the view ray and the direction toward the light (1 = looking straight into it).
float NebulaPhaseHG(float CosTheta, float G)
{
    float G2 = G * G;
    float Denom = max(1.0 + G2 - 2.0 * G * CosTheta, 1.0e-4);
    return (1.0 - G2) / (Denom * sqrt(Denom));
}

// Two-lobe phase: a forward lobe (silver lining toward the light) blended with a backward one
// (the lit face seen with the light behind the camera). BackG > 0 is the backward lobe's
// strength (used as -BackG).
float NebulaPhaseDual(float CosTheta, float ForwardG, float BackG, float BackWeight)
{
    return lerp(NebulaPhaseHG(CosTheta, ForwardG), NebulaPhaseHG(CosTheta, -BackG), BackWeight);
}

// A camera-relative point in a zone's unit space (|result| = 1 on the zone surface).
// Centre: the zone centre (camera-relative); Rows: local axis / radius per row.
float3 NebulaZoneLocal(float3 Rel, float3 Centre, float3 Row0, float3 Row1, float3 Row2)
{
    float3 D = Rel - Centre;
    return float3(dot(Row0, D), dot(Row1, D), dot(Row2, D));
}

// Ray (from the camera, Rel = Dir * t) against a zone's sphere of radius sqrt(RadiusSq) in its
// unit space (1 = the zone surface): x = entry, y = exit distance along Dir; x > y when the ray
// misses.
float2 NebulaZoneSpan(float3 Dir, float3 Centre, float3 Row0, float3 Row1, float3 Row2,
                      float RadiusSq)
{
    float3 O = NebulaZoneLocal(float3(0.0, 0.0, 0.0), Centre, Row0, Row1, Row2);
    float3 L = float3(dot(Row0, Dir), dot(Row1, Dir), dot(Row2, Dir));
    float A = max(dot(L, L), 1.0e-12);
    float B = dot(O, L);
    float C = dot(O, O) - RadiusSq;
    float Disc = B * B - A * C;
    if (Disc < 0.0)
        return float2(1.0, 0.0);
    float Root = sqrt(Disc);
    return float2((-B - Root) / A, (-B + Root) / A);
}

// The nebula's outer shell at a normalised radius Qd (|zone local|, already displaced by the
// shell noise; 1 = the zone surface). Outer: 0 in the core, rising to 1 at the surface across
// the shell (InvWidth = 1 / its width as a fraction of the radius). Returns the density of the
// medium inside the zone: 1 - Outer^2 (the inner part of the shell stays nearly full, the outer
// part thins out to 0 at the surface), times 1 + ExteriorTerm * Outer (exterior_shell_density
// - 1, weighted by how far outside the camera is: 0 from inside).
float NebulaShellProfile(float Qd, float InvWidth, float ExteriorTerm, out float Outer)
{
    Outer = saturate((Qd - 1.0) * InvWidth + 1.0);
    return (1.0 - Outer * Outer) * (1.0 + ExteriorTerm * Outer);
}

// The thin haze halo around the zone: HaloDensity at HaloEnd - 1 / InvHalo (the surface) and
// inside, falling quadratically to 0 at HaloEnd (1 + halo_extent).
float NebulaHalo(float Qd, float HaloEnd, float InvHalo, float HaloDensity)
{
    float H = saturate((HaloEnd - Qd) * InvHalo);
    return HaloDensity * H * H;
}

// Immersion in an exclusion zone, as ZONE::GetImmersion ramps it: 1 in the core, falling to 0
// over the outer edge fraction of the (normalised) radius. InvEdgeFraction: 1 / that fraction
// (the CPU clamps the fraction to at least 0.001).
float NebulaExclusionImmersion(float3 Local, float InvEdgeFraction)
{
    float Q = length(Local);
    return saturate((1.0 - Q) * InvEdgeFraction);
}

// The noise volume (64^3, wrap): r Perlin-Worley (4 cells per tile, the cloud base shape),
// g Worley fBm (8 cells), b Worley fBm (16 cells) -- the edge erosion detail -- and a, a
// low-frequency Perlin used for the colour blend.

// Base shape -> cloud amount 0..1. Remap.x = the r value where the cloud starts, Remap.y =
// 1 / (the ramp width): both from the coverage, through the noise volume's histogram (CPU).
float NebulaCloudFromBase(float Base, float2 Remap)
{
    return saturate((Base - Remap.x) * Remap.y);
}

// Erodes the cloud's edges with the detail noise (Schneider, "The real-time volumetric
// cloudscapes of Horizon Zero Dawn"): the detail value times Erosion is cut away, what stays
// is stretched back to 0..1, so the dense cores survive and the thin edges break up.
// Wispiness blends the billowy detail (Worley) toward its inverse (wisps).
float NebulaErode(float Cloud, float2 DetailGB, float Erosion, float Wispiness)
{
    float Detail = DetailGB.x * 0.625 + DetailGB.y * 0.375;
    Detail = lerp(Detail, 1.0 - Detail, Wispiness);
    float Cut = Detail * Erosion;
    return saturate((Cloud - Cut) / max(1.0 - Cut, 1.0e-3));
}

#endif
