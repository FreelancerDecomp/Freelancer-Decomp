// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's first fixed-function pixel stage: texture stage 0 and the alpha test, the part of
// Direct3D 8's pipeline the 2D draws use. SDL_GPU binding conventions: fragment textures and
// samplers in space2, fragment uniforms in space3.

Texture2D<float4> Texture0 : register(t0, space2);
SamplerState Sampler0 : register(s0, space2);

cbuffer FStage : register(b0, space3)
{
    // x: COLOROP, y: ALPHAOP (D3DTEXTUREOP: 1 disable, 2 select arg1 (texture), 3 select arg2
    // (diffuse), 4 modulate); z: 1 when a texture is bound; w: unused.
    uint4 Ops;
    // x: alpha test on; y: ALPHAFUNC (D3DCMPFUNC 1..8); z: ALPHAREF 0..255.
    uint4 AlphaTest;
};

struct FInput
{
    float4 Diffuse : TEXCOORD0;
    float2 Tex0 : TEXCOORD1;
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

float4 main(FInput In) : SV_Target0
{
    const float4 Texel = Ops.z != 0u ? Texture0.Sample(Sampler0, In.Tex0) : float4(1.0, 1.0, 1.0, 1.0);
    float4 Colour;
    Colour.rgb = Combine(Ops.x, Texel, In.Diffuse).rgb;
    Colour.a = Combine(Ops.y, Texel, In.Diffuse).a;
    // Direct3D compares alpha as an 8-bit value against ALPHAREF.
    if (AlphaTest.x != 0u && !Passes(AlphaTest.y, round(Colour.a * 255.0), float(AlphaTest.z)))
        discard;
    return Colour;
}
