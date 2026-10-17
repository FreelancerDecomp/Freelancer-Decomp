// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Shared helpers of the RP9 Ex shaders (port-only; RP9.dll / Direct3D 9, vs_3_0 + ps_3_0).
// Included as #include "common.hlsli": RP9 resolves includes next to the including shader
// first, then from the DATA_EXTRA root. See docs/re/rp9.md, "Ex extensions".

#ifndef RPEX_COMMON_HLSLI
#define RPEX_COMMON_HLSLI

// IRPDrawEx::DrawFullscreenQuad vertices: FVF XYZ|TEX1, the position already in clip space
// and already shifted by Direct3D 9's half pixel, so the vertex shader only passes it on.
struct FFullscreenVertex
{
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

FFullscreenVertex RPExFullscreenVS(float3 Position, float2 TexCoord)
{
    FFullscreenVertex Out;
    Out.Position = float4(Position.xy, 0.0, 1.0);
    Out.TexCoord = TexCoord;
    return Out;
}

// Hardware depth (the INTZ scene depth, .r) -> view-space distance. ProjZ holds the z and w
// rows of the projection the scene was drawn with: (A, B, C, D) with
// depth = (A z + B) / (C z + D), so z = (B - depth D) / (depth C - A). For the usual
// perspective matrix (C = 1, D = 0, A = f / (f - n), B = -n f / (f - n)) this is
// n f / (f - depth (f - n)).
float RPExLinearDepth(float DeviceDepth, float4 ProjZ)
{
    return abs((ProjZ.y - DeviceDepth * ProjZ.w) / (DeviceDepth * ProjZ.z - ProjZ.x));
}

float RPExLuminance(float3 Rgb)
{
    return dot(Rgb, float3(0.2126, 0.7152, 0.0722));
}

// HDR -> 0..1. Mode 0: clamp; 1: Reinhard on luminance (hue kept, White = the luminance that
// maps to 1); 2: the ACES filmic fit (Narkowicz 2015), per channel.
float3 RPExTonemap(float3 Rgb, float Mode, float White)
{
    Rgb = max(Rgb, 0.0);
    if (Mode > 1.5)
        return saturate((Rgb * (2.51 * Rgb + 0.03)) / (Rgb * (2.43 * Rgb + 0.59) + 0.14));
    if (Mode > 0.5)
    {
        float Lum = RPExLuminance(Rgb);
        float Mapped = Lum * (1.0 + Lum / max(White * White, 1.0e-4)) / (1.0 + Lum);
        return saturate(Rgb * (Mapped / max(Lum, 1.0e-6)));
    }
    return saturate(Rgb);
}

#endif
