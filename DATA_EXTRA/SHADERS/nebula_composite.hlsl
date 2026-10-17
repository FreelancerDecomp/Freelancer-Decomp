// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Volumetric nebula, pass 2: the composite (port-only; freelancer.exe Client/nebula_volume.cpp).
// Upsamples the reduced-resolution ray-march result (nebula_volume.hlsl: rgb in-scattered
// light, HDR on an FP16 target; a transmittance) onto the back buffer with blending
// ONE, SRCALPHA:
//   back buffer = scene * a + tonemap(rgb * exposure).
// Only the volume's own light is tonemapped; the scene behind it is attenuated, not remapped.
// With an 8-bit ray-march target (and in the debug views) the ray march already wrote
// display values and Tonemap.y is 0 (plain clamp).
// Depth-aware: where the four nearest low-resolution samples see about the same depth as this
// pixel it filters bilinearly; across a depth edge (a ship against the fog) it takes the
// sample whose depth matches best, so no fog halo bleeds over silhouettes.
// Drawn once per nebula, farthest first; a far nebula drawn in the background pass (before the
// opaque scene, Upsample.w) filters plainly.
// See docs/re/rp9.md, "Volumetric nebula".

#include "common.hlsli"

// s0: the INTZ scene depth (point, clamp). s1: the ray-march target (linear, clamp).
sampler2D SceneDepth : register(s0);
sampler2D Volume : register(s1);

float4 ProjZ : register(c0);       // projection z/w rows (RPExLinearDepth)
float4 LowSize : register(c1);     // ray-march target: width, height, 1/width, 1/height
float4 Upsample : register(c2);    // xy half a full-res texel, z relative depth threshold,
                                   // w 1 = no depth test (drawn before the opaque scene: plain
                                   // bilinear)
float4 Tonemap : register(c3);     // x exposure, y mode (0 clamp, 1 Reinhard luma, 2 ACES),
                                   // z Reinhard white

FFullscreenVertex VSMain(float3 Position : POSITION, float2 TexCoord : TEXCOORD0)
{
    return RPExFullscreenVS(Position, TexCoord);
}

float LinearDepthAt(float2 Uv)
{
    return RPExLinearDepth(tex2Dlod(SceneDepth, float4(Uv, 0.0, 0.0)).r, ProjZ);
}

float4 FinishVolume(float4 Volume)
{
    return float4(RPExTonemap(Volume.rgb * Tonemap.x, Tonemap.y, Tonemap.z), Volume.a);
}

float4 PSComposite(float2 TexCoord : TEXCOORD0) : COLOR0
{
    // A far nebula drawn in the background pass (no opaque scene yet): nothing to keep apart.
    if (Upsample.w > 0.5)
        return FinishVolume(tex2Dlod(Volume, float4(TexCoord, 0.0, 0.0)));

    float Depth = LinearDepthAt(TexCoord);

    // The four low-resolution texels around this pixel.
    float2 Low = TexCoord * LowSize.xy - 0.5;
    float2 Base = floor(Low);
    float2 Frac = Low - Base;
    float2 Uv00 = (Base + float2(0.5, 0.5)) * LowSize.zw;
    float2 Uv10 = Uv00 + float2(LowSize.z, 0.0);
    float2 Uv01 = Uv00 + float2(0.0, LowSize.w);
    float2 Uv11 = Uv00 + LowSize.zw;

    // Their depths: the full-res texel each one was marched with (half a texel in).
    float D00 = LinearDepthAt(Uv00 + Upsample.xy);
    float D10 = LinearDepthAt(Uv10 + Upsample.xy);
    float D01 = LinearDepthAt(Uv01 + Upsample.xy);
    float D11 = LinearDepthAt(Uv11 + Upsample.xy);
    float4 Diff = abs(float4(D00, D10, D01, D11) - Depth) / max(Depth, 1.0e-3);
    float MaxDiff = max(max(Diff.x, Diff.y), max(Diff.z, Diff.w));

    if (MaxDiff < Upsample.z)
        return FinishVolume(tex2Dlod(Volume, float4(TexCoord, 0.0, 0.0))); // smooth: bilinear

    // Depth edge: the best-matching sample (read at its texel centre).
    float2 BestUv = Uv00;
    float Best = Diff.x;
    if (Diff.y < Best)
    {
        Best = Diff.y;
        BestUv = Uv10;
    }
    if (Diff.z < Best)
    {
        Best = Diff.z;
        BestUv = Uv01;
    }
    if (Diff.w < Best)
    {
        BestUv = Uv11;
    }
    return FinishVolume(tex2Dlod(Volume, float4(BestUv, 0.0, 0.0)));
}
