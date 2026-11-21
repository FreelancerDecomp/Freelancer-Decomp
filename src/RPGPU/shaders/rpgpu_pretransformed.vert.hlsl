// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's vertex shader for pre-transformed vertices (D3DFVF_XYZRHW): the menus, the HUD and
// every other 2D draw. Position is already in screen pixels; this only maps it to clip space the
// way Direct3D 8 rasterizes it. SDL_GPU binding conventions: vertex uniforms in space1.

cbuffer FScreen : register(b0, space1)
{
    // xy: 2 / viewport width, -2 / viewport height; zw: the viewport's top-left corner in pixels.
    float4 Scale;
    // x: the half-pixel shift between Direct3D 8's pixel centres (integers) and today's (+0.5).
    float4 Offset;
};

struct FInput
{
    float4 Position : TEXCOORD0; // x, y in pixels, z depth 0..1, rhw = 1 / w
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
    const float W = 1.0 / In.Position.w;
    const float2 Pixel = In.Position.xy - Scale.zw + Offset.xx;
    // Clip space times w, so the GPU's divide gives back the pixel position and texture
    // coordinates stay perspective-correct, as with RHW in Direct3D 8.
    Out.Position = float4((Pixel.x * Scale.x - 1.0) * W, (Pixel.y * Scale.y + 1.0) * W, In.Position.z * W, W);
    Out.Diffuse = In.Diffuse.zyxw; // BGRA in memory -> RGBA
    Out.Tex0 = In.Tex0;
    return Out;
}
