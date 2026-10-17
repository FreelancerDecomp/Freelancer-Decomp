// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// [Debug] scene_depth_view (DATA_EXTRA/VOLUMETRIC/volumetric.ini): the scene depth as greyscale.
// Drawn by freelancer.exe (render_ex.cpp) over the back buffer after the 3D space scene,
// before the HUD. Near is bright, far is dark; pixels nothing was drawn on (depth 1.0, the
// cleared value) are dark blue.

#include "common.hlsli"

// s0: the INTZ scene depth, point sampled, clamped.
sampler2D SceneDepth : register(s0);

// c0: the projection's z/w rows (see RPExLinearDepth).
float4 ProjZ : register(c0);
// c1: x near distance, y far distance, z mode (0 = logarithmic near..far, 1 = linear),
//     w the distance shown black in linear mode.
float4 DepthView : register(c1);

FFullscreenVertex VSMain(float3 Position : POSITION, float2 TexCoord : TEXCOORD0)
{
    return RPExFullscreenVS(Position, TexCoord);
}

float4 PSMain(float2 TexCoord : TEXCOORD0) : COLOR0
{
    float Device = tex2D(SceneDepth, TexCoord).r;
    if (Device >= 1.0)
        return float4(0.0, 0.0, 0.15, 1.0);

    float Distance = RPExLinearDepth(Device, ProjZ);
    float Grey;
    if (DepthView.z < 0.5)
    {
        float Near = max(DepthView.x, 0.001);
        float Far = max(DepthView.y, Near * 1.001);
        Grey = saturate(log(max(Distance, Near) / Near) / log(Far / Near));
    }
    else
    {
        Grey = saturate(Distance / max(DepthView.w, 0.001));
    }
    float Shade = 1.0 - Grey;
    return float4(Shade, Shade, Shade, 1.0);
}
