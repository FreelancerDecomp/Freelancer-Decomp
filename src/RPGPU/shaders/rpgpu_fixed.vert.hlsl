// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's vertex shader: Direct3D 8's fixed-function transform. Pre-transformed vertices
// (D3DFVF_XYZRHW: the menus, the HUD) are already in screen pixels; the others go through
// world * view * projection. Either way the result lands on the pixels Direct3D 8 would
// rasterize. SDL_GPU binding conventions: vertex uniforms in space1.

cbuffer FTransform : register(b0, space1)
{
    // Direct3D's world * view * projection, for row vectors (v' = v * M), row-major as Direct3D
    // stores it.
    row_major float4x4 WorldViewProjection;
    // xy: 2 / viewport width, -2 / viewport height; zw: the viewport's top-left corner in pixels.
    float4 Scale;
    // x: the half-pixel shift between Direct3D 8's pixel centres (integers) and today's (+0.5);
    // y: 1 for pre-transformed vertices, 0 for transformed ones.
    float4 Mode;
};

struct FInput
{
    float4 Position : TEXCOORD0; // pre-transformed: x, y in pixels, z 0..1, 1 / w; else x, y, z, 1
    float4 Diffuse : TEXCOORD1;  // D3DCOLOR as B, G, R, A (UNORM8x4)
    float2 Tex0 : TEXCOORD2;
};

struct FOutput
{
    float4 Diffuse : TEXCOORD0;
    float2 Tex0 : TEXCOORD1;
    float4 Position : SV_Position;
};

FOutput main(FInput In)
{
    FOutput Out;
    if (Mode.y != 0.0)
    {
        const float W = 1.0 / In.Position.w;
        const float2 Pixel = In.Position.xy - Scale.zw + Mode.xx;
        // Clip space times w, so the GPU's divide gives back the pixel position and texture
        // coordinates stay perspective-correct, as with RHW in Direct3D 8.
        Out.Position = float4((Pixel.x * Scale.x - 1.0) * W, (Pixel.y * Scale.y + 1.0) * W, In.Position.z * W, W);
    }
    else
    {
        // Direct3D's clip space is SDL_GPU's (x, y -1..1 with y up, z 0..1); only the pixel
        // centres differ, by half a pixel, which is Scale.xy * Mode.x in normalized units.
        const float4 Clip = mul(float4(In.Position.xyz, 1.0), WorldViewProjection);
        Out.Position = float4(Clip.xy + Scale.xy * Mode.x * Clip.w, Clip.zw);
    }
    Out.Diffuse = In.Diffuse.zyxw; // BGRA in memory -> RGBA
    Out.Tex0 = In.Tex0;
    return Out;
}
