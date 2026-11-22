// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's fixed-function pixel stage: texture stage 0, the specular add, fog and the alpha test,
// in Direct3D 8's order. SDL_GPU binding conventions: fragment textures and samplers in space2,
// fragment uniforms in space3. FStage is FRpGpuStageUniforms (backend/RpGpuBackend.h).

Texture2D<float4> Texture0 : register(t0, space2);
SamplerState Sampler0 : register(s0, space2);

cbuffer FStage : register(b0, space3)
{
    // x: COLOROP, y: ALPHAOP (D3DTEXTUREOP: 1 disable, 2 select arg1 (texture), 3 select arg2
    // (diffuse), 4 modulate); z: 1 when a texture is bound; w: unused.
    uint4 Ops;
    // x: alpha test on; y: ALPHAFUNC (D3DCMPFUNC 1..8); z: ALPHAREF 0..255.
    uint4 AlphaTest;
    float4 FogColor;
    float4 FogTable; // FOGTABLEMODE (1 exp, 2 exp2, 3 linear), FOGSTART, FOGEND, FOGDENSITY
    // x: fog (0 off, 1 the interpolated vertex fog value, 2 table fog); y: table fog on w rather
    // than z; z: SPECULARENABLE.
    uint4 FogFlags;
};

struct FInput
{
    float4 Diffuse : TEXCOORD0;
    float4 Specular : TEXCOORD1;
    float2 Tex0 : TEXCOORD2;
    float2 Fog : TEXCOORD3;
    float4 Position : SV_Position;
};

float4 Combine(uint Op, float4 Texel, float4 Diffuse)
{
    if (Op == 2u)
        return Texel;
    if (Op == 4u)
        return Texel * Diffuse;
    return Diffuse; // 1 (disable: the diffuse passes through) and 3
}

bool Passes(uint Func, float Value, float Ref)
{
    switch (Func)
    {
    case 1u: return false;          // NEVER
    case 2u: return Value < Ref;    // LESS
    case 3u: return Value == Ref;   // EQUAL
    case 4u: return Value <= Ref;   // LESSEQUAL
    case 5u: return Value > Ref;    // GREATER
    case 6u: return Value != Ref;   // NOTEQUAL
    case 7u: return Value >= Ref;   // GREATEREQUAL
    default: return true;           // ALWAYS
    }
}

float TableFog(float Distance)
{
    const float Mode = FogTable.x;
    const float Density = FogTable.w;
    if (Mode == 1.0)
        return exp(-Density * Distance);
    if (Mode == 2.0)
        return exp(-(Density * Distance) * (Density * Distance));
    return (FogTable.z - Distance) / (FogTable.z - FogTable.y); // linear
}

float4 main(FInput In) : SV_Target0
{
    // With no texture bound, the texture argument reads the diffuse colour, as in Direct3D.
    const float4 Texel = Ops.z != 0u ? Texture0.Sample(Sampler0, In.Tex0) : In.Diffuse;
    float4 Colour;
    if (Ops.x == 1u)
    {
        // COLOROP disabled turns the whole stage off, alpha included: the diffuse passes through.
        Colour = In.Diffuse;
    }
    else
    {
        Colour.rgb = Combine(Ops.x, Texel, In.Diffuse).rgb;
        Colour.a = Combine(Ops.y, Texel, In.Diffuse).a;
    }
    if (FogFlags.z != 0u)
        Colour.rgb = saturate(Colour.rgb + In.Specular.rgb);
    if (FogFlags.x != 0u)
    {
        // Table fog on w (the camera-space depth) or z (the depth buffer's value).
        const float Factor = FogFlags.x == 2u ? TableFog(FogFlags.y != 0u ? In.Fog.y : In.Position.z) : In.Fog.x;
        Colour.rgb = lerp(FogColor.rgb, Colour.rgb, saturate(Factor));
    }
    // Direct3D compares alpha as an 8-bit value against ALPHAREF.
    if (AlphaTest.x != 0u && !Passes(AlphaTest.y, round(Colour.a * 255.0), float(AlphaTest.z)))
        discard;
    return Colour;
}
