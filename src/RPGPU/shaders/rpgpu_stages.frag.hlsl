// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's fixed-function pixel stage: Direct3D 8's texture stages (up to four, every
// D3DTEXTUREOP and D3DTA argument but the bump-mapping ones), the specular add, fog and the alpha
// test, in Direct3D's order. SDL_GPU binding conventions: fragment textures and samplers in
// space2, fragment uniforms in space3. FStages is FRpGpuStageUniforms (backend/RpGpuBackend.h).

Texture2D<float4> Texture0 : register(t0, space2);
SamplerState Sampler0 : register(s0, space2);
Texture2D<float4> Texture1 : register(t1, space2);
SamplerState Sampler1 : register(s1, space2);
Texture2D<float4> Texture2 : register(t2, space2);
SamplerState Sampler2 : register(s2, space2);
Texture2D<float4> Texture3 : register(t3, space2);
SamplerState Sampler3 : register(s3, space2);

cbuffer FStages : register(b0, space3)
{
    uint4 Color[4]; // per stage: COLOROP, COLORARG1, COLORARG2, COLORARG0
    uint4 Alpha[4]; // per stage: ALPHAOP, ALPHAARG1, ALPHAARG2, ALPHAARG0
    // Per stage: RESULTARG; 1 when a texture is bound; the projected divide's component (0 none).
    uint4 Misc[4];
    float4 TextureFactor;
    uint4 AlphaTest; // x: on; y: ALPHAFUNC (D3DCMPFUNC 1..8); z: ALPHAREF 0..255
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
    float2 Fog : TEXCOORD2;
    float4 Tex0 : TEXCOORD3;
    float4 Tex1 : TEXCOORD4;
    float4 Tex2 : TEXCOORD5;
    float4 Tex3 : TEXCOORD6;
    float4 Position : SV_Position;
};

// D3DTA: 0 diffuse, 1 current, 2 texture, 3 texture factor, 4 specular, 5 temp; 0x10 complement,
// 0x20 alpha replicate.
float4 Argument(uint Arg, float4 Diffuse, float4 Current, float4 Texel, float4 Specular, float4 Temp)
{
    float4 Value;
    switch (Arg & 15u)
    {
    case 0u: Value = Diffuse; break;
    case 2u: Value = Texel; break;
    case 3u: Value = TextureFactor; break;
    case 4u: Value = Specular; break;
    case 5u: Value = Temp; break;
    default: Value = Current; break;
    }
    if ((Arg & 0x20u) != 0u)
        Value = Value.aaaa;
    if ((Arg & 0x10u) != 0u)
        Value = 1.0 - Value;
    return Value;
}

// D3DTEXTUREOP on three arguments (A0 for MULTIPLYADD and LERP). Blend factors read the
// diffuse, texture, factor and current alphas. The bump-mapping ops (22, 23) are not drawn and
// pass Current through, as does PREMODULATE (17), which only modulates the next stage.
float4 Operation(uint Op, float4 A1, float4 A2, float4 A0, float4 Diffuse, float4 Current, float4 Texel)
{
    switch (Op)
    {
    case 2u: return A1;                                   // SELECTARG1
    case 3u: return A2;                                   // SELECTARG2
    case 4u: return A1 * A2;                              // MODULATE
    case 5u: return A1 * A2 * 2.0;                        // MODULATE2X
    case 6u: return A1 * A2 * 4.0;                        // MODULATE4X
    case 7u: return A1 + A2;                              // ADD
    case 8u: return A1 + A2 - 0.5;                        // ADDSIGNED
    case 9u: return (A1 + A2 - 0.5) * 2.0;                // ADDSIGNED2X
    case 10u: return A1 - A2;                             // SUBTRACT
    case 11u: return A1 + A2 - A1 * A2;                   // ADDSMOOTH
    case 12u: return lerp(A2, A1, Diffuse.a);             // BLENDDIFFUSEALPHA
    case 13u: return lerp(A2, A1, Texel.a);               // BLENDTEXTUREALPHA
    case 14u: return lerp(A2, A1, TextureFactor.a);       // BLENDFACTORALPHA
    case 15u: return A1 + A2 * (1.0 - Texel.a);           // BLENDTEXTUREALPHAPM
    case 16u: return lerp(A2, A1, Current.a);             // BLENDCURRENTALPHA
    case 18u: return float4(A1.rgb + A1.a * A2.rgb, A1.a); // MODULATEALPHA_ADDCOLOR
    case 19u: return float4(A1.rgb * A2.rgb + A1.a, A1.a); // MODULATECOLOR_ADDALPHA
    case 20u: return float4((1.0 - A1.a) * A2.rgb + A1.rgb, A1.a); // MODULATEINVALPHA_ADDCOLOR
    case 21u: return float4((1.0 - A1.rgb) * A2.rgb + A1.a, A1.a); // MODULATEINVCOLOR_ADDALPHA
    case 24u: return dot(A1.rgb - 0.5, A2.rgb - 0.5).xxxx * 4.0; // DOTPRODUCT3, every channel
    case 25u: return A0 + A1 * A2;                        // MULTIPLYADD
    case 26u: return lerp(A2, A1, A0);                    // LERP: A0 * A1 + (1 - A0) * A2
    default: return Current;                              // PREMODULATE, the bump-map ops
    }
}

float4 Sample(uint Stage, float4 Coord)
{
    // PROJECTED divides by the last component the transform produced.
    const uint Divide = Misc[Stage].z;
    float2 Uv = Coord.xy;
    if (Divide == 2u)
        Uv = Coord.xy / Coord.y;
    else if (Divide == 3u)
        Uv = Coord.xy / Coord.z;
    else if (Divide == 4u)
        Uv = Coord.xy / Coord.w;
    if (Stage == 0u)
        return Texture0.Sample(Sampler0, Uv);
    if (Stage == 1u)
        return Texture1.Sample(Sampler1, Uv);
    if (Stage == 2u)
        return Texture2.Sample(Sampler2, Uv);
    return Texture3.Sample(Sampler3, Uv);
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
    const float4 Coords[4] = {In.Tex0, In.Tex1, In.Tex2, In.Tex3};
    float4 Current = In.Diffuse; // stage 0's CURRENT is the diffuse colour
    float4 Temp = float4(0.0, 0.0, 0.0, 0.0);
    [unroll]
    for (uint Stage = 0u; Stage < 4u; ++Stage)
    {
        // The first stage whose COLOROP is DISABLE ends the cascade, alpha included.
        if (Color[Stage].x == 1u)
            break;
        // With no texture bound, the texture argument reads the diffuse colour (RPGPU's rule
        // since the 2D draws).
        const float4 Texel = Misc[Stage].y != 0u ? Sample(Stage, Coords[Stage]) : In.Diffuse;
        float4 Result;
        const float4 C1 = Argument(Color[Stage].y, In.Diffuse, Current, Texel, In.Specular, Temp);
        const float4 C2 = Argument(Color[Stage].z, In.Diffuse, Current, Texel, In.Specular, Temp);
        const float4 C0 = Argument(Color[Stage].w, In.Diffuse, Current, Texel, In.Specular, Temp);
        const float4 ColourOut = Operation(Color[Stage].x, C1, C2, C0, In.Diffuse, Current, Texel);
        Result.rgb = ColourOut.rgb;
        if (Color[Stage].x == 24u)
        {
            Result.a = ColourOut.a; // DOTPRODUCT3 writes alpha too, whatever ALPHAOP says
        }
        else if (Alpha[Stage].x == 1u)
        {
            Result.a = Current.a; // ALPHAOP DISABLE: the alpha passes on
        }
        else
        {
            const float4 A1 = Argument(Alpha[Stage].y, In.Diffuse, Current, Texel, In.Specular, Temp);
            const float4 A2 = Argument(Alpha[Stage].z, In.Diffuse, Current, Texel, In.Specular, Temp);
            const float4 A0 = Argument(Alpha[Stage].w, In.Diffuse, Current, Texel, In.Specular, Temp);
            // Alpha operations work on the arguments' alphas, except DOTPRODUCT3, which takes
            // the dot product of their colours.
            Result.a = Alpha[Stage].x == 24u
                           ? Operation(24u, A1, A2, A0, In.Diffuse, Current, Texel).a
                           : Operation(Alpha[Stage].x, A1.aaaa, A2.aaaa, A0.aaaa, In.Diffuse, Current, Texel).a;
        }
        Result = saturate(Result);
        if (Misc[Stage].x == 5u)
            Temp = Result; // RESULTARG TEMP
        else
            Current = Result;
    }

    float4 Colour = Current;
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
