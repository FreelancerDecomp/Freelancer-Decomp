// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's vertex shader: Direct3D 8's fixed-function vertex stage. Pre-transformed vertices
// (D3DFVF_XYZRHW: the menus, the HUD) are already in screen pixels; the others go through
// world * view * projection, are lit per vertex and fogged per vertex when the state asks for
// it. The result lands on the pixels Direct3D 8 would rasterize. SDL_GPU binding conventions:
// vertex uniforms in space1. The blocks are FRpGpuTransformUniforms and FRpGpuLightingUniforms
// (backend/RpGpuBackend.h).

cbuffer FTransform : register(b0, space1)
{
    // Direct3D's matrices, for row vectors (v' = v * M), row-major as Direct3D stores them.
    row_major float4x4 WorldViewProjection;
    row_major float4x4 World;
    row_major float4x4 View;
    row_major float4x4 NormalMatrix; // the inverse transpose of World's 3x3
    // xy: 2 / viewport width, -2 / viewport height; zw: the viewport's top-left corner in pixels.
    float4 Scale;
    // x: the half-pixel shift between Direct3D 8's pixel centres (integers) and today's (+0.5);
    // y: 1 for pre-transformed vertices, 0 for transformed ones.
    float4 Mode;
    float4 Eye;    // xyz: the camera, in world space
    float4 Viewer; // xyz: towards a non-local viewer, in world space
    // x: lighting; y: the vertex has a normal (1), diffuse (2), specular (4); z: NORMALIZENORMALS;
    // w: LOCALVIEWER.
    uint4 Flags;
    float4 Fog;     // FOGVERTEXMODE (1 exp, 2 exp2, 3 linear), FOGSTART, FOGEND, FOGDENSITY
    uint4 FogFlags; // x: vertex fog; y: RANGEFOGENABLE
    // Per stage: the coordinate source (0 the vertex's set, 1 camera-space normal, 2 camera-space
    // position, 3 camera-space reflection vector) and, in bits 4..7, the set's component count.
    uint4 TexGen;
    uint4 TexTransform; // per stage: TEXTURETRANSFORMFLAGS' count, 0 for none
    row_major float4x4 TexMatrix[4];
    row_major float4x4 ViewProjection; // for blended vertices
    uint4 Blend; // x: VERTEXBLEND's weight count (0 off, 1..3); y: INDEXEDVERTEXBLENDENABLE
};

// The world matrices for vertex blending: matrix i is Palette[i * 3 + c], the column c of its
// 4x3 part, so a row vector's coordinate c is dot(float4(p, 1), column).
cbuffer FBlend : register(b2, space1)
{
    float4 Palette[768];
};

struct FLight
{
    float4 Diffuse;
    float4 Specular;
    float4 Ambient;
    float4 Position;    // w: 1 point, 2 spot, 3 directional
    float4 Direction;   // w: range
    float4 Attenuation; // 0, 1, 2; w: falloff
    float4 Spot;        // x: cos(theta / 2), y: cos(phi / 2)
};

cbuffer FLighting : register(b1, space1)
{
    float4 GlobalAmbient;
    float4 MaterialDiffuse;
    float4 MaterialAmbient;
    float4 MaterialSpecular;
    float4 MaterialEmissive;
    float4 Power;  // x
    uint4 Sources; // diffuse, ambient, specular, emissive: 0 material, 1 vertex diffuse, 2 vertex specular
    uint4 Count;   // x: lights
    FLight Lights[8];
};

struct FInput
{
    float4 Position : TEXCOORD0; // pre-transformed: x, y in pixels, z 0..1, 1 / w; else x, y, z, 1
    float4 Diffuse : TEXCOORD1;  // D3DCOLOR as B, G, R, A (UNORM8x4)
    float3 Normal : TEXCOORD2;
    float4 Specular : TEXCOORD3; // D3DCOLOR as B, G, R, A
    float2 Tex0 : TEXCOORD4;     // per stage: its texture-coordinate set's first two components
    float2 Tex1 : TEXCOORD5;
    float2 Tex2 : TEXCOORD6;
    float2 Tex3 : TEXCOORD7;
    float3 Weights : TEXCOORD8; // the first three blend weights
    uint4 Indices : TEXCOORD9;  // four world-matrix indices
};

struct FOutput
{
    float4 Diffuse : TEXCOORD0;
    float4 Specular : TEXCOORD1;
    float2 Fog : TEXCOORD2; // x: the vertex fog factor; y: w, for table fog on w
    float4 Tex0 : TEXCOORD3; // per stage, after generation and the texture transform
    float4 Tex1 : TEXCOORD4;
    float4 Tex2 : TEXCOORD5;
    float4 Tex3 : TEXCOORD6;
    float4 Position : SV_Position;
};

// A material colour from its source (D3DMATERIALCOLORSOURCE), the material when the vertex lacks
// the colour named.
float4 Source(uint Which, float4 Material, float4 VertexDiffuse, float4 VertexSpecular)
{
    if (Which == 1u && (Flags.y & 2u) != 0u)
        return VertexDiffuse;
    if (Which == 2u && (Flags.y & 4u) != 0u)
        return VertexSpecular;
    return Material;
}

float FogFactor(float Mode, float Distance, float Start, float End, float Density)
{
    if (Mode == 1.0)
        return exp(-Density * Distance);
    if (Mode == 2.0)
        return exp(-(Density * Distance) * (Density * Distance));
    return (End - Distance) / (End - Start); // linear
}

// One stage's coordinates: the vertex's set or a camera-space vector, then the stage's texture
// transform. A set with fewer components is padded as Direct3D pads it before the transform:
// a 1 after the last component, 0 after that, so (u, v) becomes (u, v, 1, 0) and the matrix's
// third row translates. Generated vectors are (x, y, z, 1).
float4 StageCoordinates(uint Stage, float2 Vertex, float3 CameraNormal, float3 CameraPosition)
{
    const uint Source = TexGen[Stage] & 15u;
    float4 Coord;
    if (Source == 1u)
        Coord = float4(CameraNormal, 1.0);
    else if (Source == 2u)
        Coord = float4(CameraPosition, 1.0);
    else if (Source == 3u)
    {
        // The camera-to-vertex direction, or camera space's +z for a non-local viewer
        // (LOCALVIEWER off), reflected about the normal.
        const float3 Incident = Flags.w != 0u ? normalize(CameraPosition) : float3(0.0, 0.0, 1.0);
        Coord = float4(reflect(Incident, CameraNormal), 1.0);
    }
    else
        Coord = ((TexGen[Stage] >> 4) & 15u) == 1u ? float4(Vertex.x, 1.0, 0.0, 0.0) : float4(Vertex, 1.0, 0.0);
    if (TexTransform[Stage] != 0u)
        return mul(Coord, TexMatrix[Stage]);
    return Coord;
}

FOutput main(FInput In)
{
    FOutput Out;
    const float4 VertexDiffuse = (Flags.y & 2u) != 0u ? In.Diffuse.zyxw : float4(1.0, 1.0, 1.0, 1.0);
    const float4 VertexSpecular = (Flags.y & 4u) != 0u ? In.Specular.zyxw : float4(0.0, 0.0, 0.0, 0.0);
    Out.Diffuse = VertexDiffuse;
    Out.Specular = VertexSpecular;
    Out.Tex0 = float4(In.Tex0, 0.0, 1.0);
    Out.Tex1 = float4(In.Tex1, 0.0, 1.0);
    Out.Tex2 = float4(In.Tex2, 0.0, 1.0);
    Out.Tex3 = float4(In.Tex3, 0.0, 1.0);

    if (Mode.y != 0.0)
    {
        const float W = 1.0 / In.Position.w;
        const float2 Pixel = In.Position.xy - Scale.zw + Mode.xx;
        // Clip space times w, so the GPU's divide gives back the pixel position and texture
        // coordinates stay perspective-correct, as with RHW in Direct3D 8.
        Out.Position = float4((Pixel.x * Scale.x - 1.0) * W, (Pixel.y * Scale.y + 1.0) * W, In.Position.z * W, W);
        // No vertex fog and no coordinate generation for pre-transformed vertices: the specular
        // alpha is the fog value.
        Out.Fog = float2(VertexSpecular.a, W);
        return Out;
    }

    float3 WorldPosition;
    float3 N = float3(0.0, 0.0, 0.0);
    float4 Clip;
    if (Blend.x != 0u)
    {
        // Direct3D's vertex blending: the weights given, the last one 1 minus their sum, each
        // on its world matrix (indexed, or 0, 1, 2, 3). Normals go through the matrices' 3x3
        // parts, as for rigid bones.
        const uint4 Index = Blend.y != 0u ? min(In.Indices, uint4(255u, 255u, 255u, 255u)) : uint4(0u, 1u, 2u, 3u);
        const float Weight[4] = {In.Weights.x, In.Weights.y, In.Weights.z, 0.0};
        const float4 P = float4(In.Position.xyz, 1.0);
        const float4 Nrm = float4(In.Normal, 0.0);
        float Remaining = 1.0;
        WorldPosition = float3(0.0, 0.0, 0.0);
        [unroll]
        for (uint Which = 0u; Which < 4u; ++Which)
        {
            if (Which > Blend.x)
                break;
            const float W = Which == Blend.x ? Remaining : Weight[Which];
            Remaining -= W;
            const uint Base = Index[Which] * 3u;
            WorldPosition += W * float3(dot(P, Palette[Base]), dot(P, Palette[Base + 1u]), dot(P, Palette[Base + 2u]));
            if ((Flags.y & 1u) != 0u)
                N += W * float3(dot(Nrm, Palette[Base]), dot(Nrm, Palette[Base + 1u]), dot(Nrm, Palette[Base + 2u]));
        }
        Clip = mul(float4(WorldPosition, 1.0), ViewProjection);
    }
    else
    {
        Clip = mul(float4(In.Position.xyz, 1.0), WorldViewProjection);
        WorldPosition = mul(float4(In.Position.xyz, 1.0), World).xyz;
        if ((Flags.y & 1u) != 0u)
            N = mul(In.Normal, (float3x3)NormalMatrix);
    }
    // Direct3D's clip space is SDL_GPU's (x, y -1..1 with y up, z 0..1); only the pixel centres
    // differ, by half a pixel, which is Scale.xy * Mode.x in normalized units.
    Out.Position = float4(Clip.xy + Scale.xy * Mode.x * Clip.w, Clip.zw);

    if (Flags.z != 0u && dot(N, N) > 0.0)
        N = normalize(N);
    const float3 ViewPosition = mul(float4(WorldPosition, 1.0), View).xyz;
    const float3 ViewNormal = mul(N, (float3x3)View);
    Out.Tex0 = StageCoordinates(0u, In.Tex0, ViewNormal, ViewPosition);
    Out.Tex1 = StageCoordinates(1u, In.Tex1, ViewNormal, ViewPosition);
    Out.Tex2 = StageCoordinates(2u, In.Tex2, ViewNormal, ViewPosition);
    Out.Tex3 = StageCoordinates(3u, In.Tex3, ViewNormal, ViewPosition);
    if (Flags.x != 0u)
    {
        // Lit in world space: the view is rigid (RP8's z flip included), so distances and angles
        // are those of Direct3D's camera space.
        const float3 ToViewer = Flags.w != 0u ? normalize(Eye.xyz - WorldPosition) : Viewer.xyz;
        const float4 Md = Source(Sources.x, MaterialDiffuse, VertexDiffuse, VertexSpecular);
        const float4 Ma = Source(Sources.y, MaterialAmbient, VertexDiffuse, VertexSpecular);
        const float4 Ms = Source(Sources.z, MaterialSpecular, VertexDiffuse, VertexSpecular);
        const float4 Me = Source(Sources.w, MaterialEmissive, VertexDiffuse, VertexSpecular);

        float3 Diffuse = Me.rgb + Ma.rgb * GlobalAmbient.rgb;
        float3 Specular = float3(0.0, 0.0, 0.0);
        for (uint Index = 0u; Index < Count.x; ++Index)
        {
            const FLight Light = Lights[Index];
            float3 L;
            float Atten = 1.0;
            if (Light.Position.w == 3.0)
            {
                L = -normalize(Light.Direction.xyz);
            }
            else
            {
                const float3 Delta = Light.Position.xyz - WorldPosition;
                const float Distance = length(Delta);
                if (Distance > Light.Direction.w)
                    continue; // out of range: no light at all
                L = Delta / max(Distance, 1e-20);
                Atten = 1.0 / (Light.Attenuation.x + Light.Attenuation.y * Distance +
                               Light.Attenuation.z * Distance * Distance);
                if (Light.Position.w == 2.0)
                {
                    const float Rho = dot(-normalize(Light.Direction.xyz), L);
                    if (Rho <= Light.Spot.y)
                        Atten = 0.0;
                    else if (Rho <= Light.Spot.x)
                        Atten *= pow(max((Rho - Light.Spot.y) / (Light.Spot.x - Light.Spot.y), 0.0), Light.Attenuation.w);
                }
            }
            const float NdotL = dot(N, L);
            Diffuse += Atten * (Ma.rgb * Light.Ambient.rgb + Md.rgb * Light.Diffuse.rgb * max(NdotL, 0.0));
            if (NdotL > 0.0)
            {
                const float NdotH = max(dot(N, normalize(ToViewer + L)), 0.0);
                const float Highlight = Power.x > 0.0 ? pow(NdotH, Power.x) : 1.0;
                Specular += Atten * Ms.rgb * Light.Specular.rgb * Highlight;
            }
        }
        Out.Diffuse = float4(saturate(Diffuse), saturate(Md.a));
        Out.Specular = float4(saturate(Specular), saturate(Ms.a));
    }

    // Vertex fog on the camera-space depth (its magnitude), or the distance with
    // RANGEFOGENABLE; otherwise the vertex's own specular alpha is the fog value, lit or not.
    float FogValue = VertexSpecular.a;
    if (FogFlags.x != 0u)
    {
        const float Distance = FogFlags.y != 0u ? length(ViewPosition) : abs(ViewPosition.z);
        FogValue = saturate(FogFactor(Fog.x, Distance, Fog.y, Fog.z, Fog.w));
    }
    Out.Fog = float2(FogValue, Clip.w);
    return Out;
}
