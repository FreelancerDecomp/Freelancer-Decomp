// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// The seam between RPGPU's pipeline (RpGpu.*, which talks to the game through RP8's interfaces)
// and what puts pixels on a screen (docs/re/rpgpu.md). Two backends:
//
// - SDL (RpGpuSdlBackend.*): an SDL_GPU device and swap chain on the client's window. The only code
//   that includes SDL.
// - Null (RpGpuNullBackend.cpp): no device. It accepts every call, reports a fixed list of display
//   modes and presents nothing, so RPGPU runs where there is no GPU: the CI smoke test and
//   screenshot comparison (x64-client-plan.md, steps 3.2 and 4.2). Selected with
//   [RenderPipeline] NULL_DEVICE = 1.

#include "render/rp_types.h"

#include <cstdint>
#include <memory>
#include <vector>

struct FRpGpuDisplayMode
{
    unsigned int Width;
    unsigned int Height;
    unsigned int Refresh;
};

// A frame read back from the backend: Width x Height pixels, 4 bytes each in B, G, R, A order,
// rows top to bottom, no padding.
struct FRpGpuFrame
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<std::uint8_t> Bgra;
};

// One frame as RPGPU records it between two presents (docs/re/rpgpu.md, "Drawing"): the
// vertices and indices of every draw, and the clears and draws in the order the game issued
// them. The backend uploads the geometry once and replays the commands in one pass per clear.

// The texture stages RPGPU draws, and reports as its blend-stage count (pipeline state 6).
inline constexpr int kRpGpuStages = 4;

// One vertex, whatever the game's own layout (FVF) was. Pre-transformed (D3DFVF_XYZRHW): X, Y
// in pixels, Z 0..1, W = 1 / w. Transformed: the model-space position, W = 1. The diffuse and
// specular colours as D3DCOLOR (B, G, R, A bytes); the normal (0 when the format has none);
// Tex: for each stage, the first two components of the texture-coordinate set its
// TEXCOORDINDEX names.
struct FRpGpuVertex
{
    float X;
    float Y;
    float Z;
    float W;
    std::uint32_t Diffuse;
    float Nx;
    float Ny;
    float Nz;
    std::uint32_t Specular;
    float Tex[kRpGpuStages][2];
    // Vertex blending: the format's first three blend weights, and its four matrix indices
    // (D3DFVF_LASTBETA_UBYTE4's bytes; 0, 1, 2, 3 without them).
    float Weights[3];
    std::uint32_t Indices;
};
static_assert(sizeof(FRpGpuVertex) == 84, "the vertex layout the shaders read");

// The world matrices vertex blending can index (Direct3D's D3DTS_WORLDMATRIX(0..255)).
inline constexpr int kRpGpuWorldMatrices = 256;

// The shaders' uniform blocks (rpgpu_fixed.vert, rpgpu_stages.frag), laid out as HLSL packs
// them: only 16-byte vectors and 4x4 matrices, so the C++ and HLSL layouts are the same.
// Matrices are Direct3D's: row vectors, row-major.

// rpgpu_fixed.vert's FTransform: one per draw.
struct FRpGpuTransformUniforms
{
    float WorldViewProjection[16];
    float World[16];
    float View[16];
    float NormalMatrix[16]; // the inverse transpose of the world's 3x3, in the upper 3x3
    float Scale[4];         // xy: 2 / viewport width, -2 / viewport height; zw: viewport origin
    float Mode[4];          // x: half-pixel shift; y: 1 pre-transformed
    float Eye[4];           // xyz: the camera's position in world space
    float Viewer[4];        // xyz: the direction to a non-local viewer, in world space
    // x: lighting on; y: the format's normal (1), diffuse (2), specular (4); z: NORMALIZENORMALS;
    // w: LOCALVIEWER.
    std::uint32_t Flags[4];
    float Fog[4];            // FOGVERTEXMODE (D3DFOGMODE), FOGSTART, FOGEND, FOGDENSITY
    std::uint32_t FogFlags[4]; // x: vertex fog computed; y: RANGEFOGENABLE
    // Per stage: the coordinate source (0 the vertex's set, 1 camera-space normal, 2
    // camera-space position, 3 camera-space reflection vector: TEXCOORDINDEX's high bits) and,
    // in bits 4..7, how many components the vertex's set has (1 or 2).
    std::uint32_t TexGen[4];
    // Per stage: TEXTURETRANSFORMFLAGS' count (0: no transform).
    std::uint32_t TexTransform[4];
    float TexMatrix[kRpGpuStages][16]; // SetTextureStageTransform's, Direct3D's layout
    float ViewProjection[16];          // for blended vertices, which are in world space
    // x: VERTEXBLEND's weight count (1..3); y: INDEXEDVERTEXBLENDENABLE; z: 1 for 0WEIGHTS (each
    // vertex on its first indexed matrix). x and z both 0: no blending.
    std::uint32_t Blend[4];
};
static_assert(sizeof(FRpGpuTransformUniforms) == 480 + 64 * kRpGpuStages, "rpgpu_fixed.vert's FTransform");

// rpgpu_fixed.vert's FBlend: the world matrices for vertex blending, each as the three columns
// of its 4x3 part (a row vector times a column gives one coordinate). Shared by the draws that
// use the same matrices (FRpGpuFrameRecording::Blend).
struct FRpGpuBlendUniforms
{
    float Columns[kRpGpuWorldMatrices][3][4];
};
static_assert(sizeof(FRpGpuBlendUniforms) == 48 * kRpGpuWorldMatrices, "rpgpu_fixed.vert's FBlend");

// One Direct3D 8 light, in world space. Position.w: D3DLIGHTTYPE (1 point, 2 spot, 3
// directional); Direction.w: range; Attenuation: 0, 1, 2 and falloff; Spot: cos(theta / 2),
// cos(phi / 2).
struct FRpGpuLight
{
    float Diffuse[4];
    float Specular[4];
    float Ambient[4];
    float Position[4];
    float Direction[4];
    float Attenuation[4];
    float Spot[4];
};

inline constexpr int kRpGpuMaxLights = 8;

// rpgpu_fixed.vert's FLighting: the material, the global ambient and the enabled lights. Shared
// by the draws that use the same values (FRpGpuFrameRecording::Lighting).
struct FRpGpuLightingUniforms
{
    float GlobalAmbient[4];
    float MaterialDiffuse[4];
    float MaterialAmbient[4];
    float MaterialSpecular[4];
    float MaterialEmissive[4];
    float Power[4];             // x: the material's power
    // DIFFUSE / AMBIENT / SPECULAR / EMISSIVEMATERIALSOURCE (D3DMATERIALCOLORSOURCE: 0
    // material, 1 diffuse, 2 specular), already 0 when COLORVERTEX is off.
    std::uint32_t Sources[4];
    std::uint32_t Count[4];     // x: lights in Lights
    FRpGpuLight Lights[kRpGpuMaxLights];
};
static_assert(sizeof(FRpGpuLightingUniforms) == 128 + 112 * kRpGpuMaxLights, "rpgpu_fixed.vert's FLighting");

// rpgpu_stages.frag's FStages: one per draw. Stages run until the first whose COLOROP is
// DISABLE.
struct FRpGpuStageUniforms
{
    // Per stage: COLOROP, COLORARG1, COLORARG2, COLORARG0 (D3DTEXTUREOP, D3DTA).
    std::uint32_t Color[kRpGpuStages][4];
    // Per stage: ALPHAOP, ALPHAARG1, ALPHAARG2, ALPHAARG0.
    std::uint32_t Alpha[kRpGpuStages][4];
    // Per stage: RESULTARG (1 current, 5 temp); 1 when a texture is bound; the projected
    // divide's component (TEXTURETRANSFORMFLAGS' count with PROJECTED, else 0); 1 when the
    // texture is a cube map (set by the backend).
    std::uint32_t Misc[kRpGpuStages][4];
    float TextureFactor[4];   // TEXTUREFACTOR, rgba 0..1
    // x: alpha test on; y: ALPHAFUNC (D3DCMPFUNC); z: ALPHAREF 0..255.
    std::uint32_t AlphaTest[4];
    float FogColor[4];        // rgb 0..1
    float FogTable[4];        // FOGTABLEMODE, FOGSTART, FOGEND, FOGDENSITY
    // x: fog (0 off, 1 the vertex's fog value, 2 table fog per pixel); y: table fog on w, not
    // z; z: SPECULARENABLE.
    std::uint32_t FogFlags[4];
};
static_assert(sizeof(FRpGpuStageUniforms) == 48 * kRpGpuStages + 80, "rpgpu_stages.frag's FStages");

enum class ERpGpuTopology : std::uint8_t
{
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
};

// Everything a GPU pipeline object depends on, as Direct3D 8 numbers: render states 7 (ZENABLE),
// 14 (ZWRITEENABLE), 23 (ZFUNC), 27 (ALPHABLENDENABLE), 19 / 20 (SRCBLEND / DESTBLEND), 0xA8
// (COLORWRITEENABLE), 22 (CULLMODE), 8 (FILLMODE) and 47 (ZBIAS).
struct FRpGpuPipelineKey
{
    ERpGpuTopology Topology = ERpGpuTopology::Triangles;
    std::uint8_t bZEnable = 0;
    std::uint8_t bZWrite = 0;
    std::uint8_t ZFunc = 4;   // D3DCMPFUNC
    std::uint8_t bBlend = 0;
    std::uint8_t SrcBlend = 2; // D3DBLEND
    std::uint8_t DstBlend = 1;
    std::uint8_t ColorWriteMask = 0xF; // R 1, G 2, B 4, A 8
    std::uint8_t CullMode = 1;         // D3DCULL: 1 none, 2 clockwise, 3 counter-clockwise
    std::uint8_t FillMode = 3;         // D3DFILLMODE: 1 point, 2 wireframe, 3 solid
    std::uint8_t ZBias = 0;            // 0..16

    // One 64-bit key. The narrow fields share bytes: ColorWriteMask has 4 bits, CullMode and
    // FillMode 2 each (their values are 1..3) and ZBias 5 (Direct3D 8 allows 0..16).
    std::uint64_t Packed() const
    {
        return static_cast<std::uint64_t>(Topology) | (static_cast<std::uint64_t>(bZEnable) << 8) |
               (static_cast<std::uint64_t>(bZWrite) << 12) | (static_cast<std::uint64_t>(ZFunc) << 16) |
               (static_cast<std::uint64_t>(bBlend) << 24) | (static_cast<std::uint64_t>(SrcBlend) << 32) |
               (static_cast<std::uint64_t>(DstBlend) << 40) | (static_cast<std::uint64_t>(ColorWriteMask) << 48) |
               (static_cast<std::uint64_t>(CullMode & 3) << 52) | (static_cast<std::uint64_t>(FillMode & 3) << 54) |
               (static_cast<std::uint64_t>(ZBias & 31) << 56);
    }
};

// A texture stage's sampler, as Direct3D 8 numbers: ADDRESSU / ADDRESSV (D3DTEXTUREADDRESS),
// MAGFILTER / MINFILTER / MIPFILTER (D3DTEXTUREFILTERTYPE; 3 anisotropic), MAXMIPLEVEL (the
// largest level sampled, 0 being the full-size one), MAXANISOTROPY and MIPMAPLODBIAS (a float's
// bits).
struct FRpGpuSamplerKey
{
    std::uint8_t AddressU = 1;
    std::uint8_t AddressV = 1;
    std::uint8_t MagFilter = 2;
    std::uint8_t MinFilter = 2;
    std::uint8_t MipFilter = 0;
    std::uint8_t MaxMipLevel = 0;
    std::uint8_t MaxAnisotropy = 1;
    std::uint32_t LodBias = 0;

    // The first 56 bits; LodBias is the rest of the cache key.
    std::uint64_t Packed() const
    {
        return static_cast<std::uint64_t>(AddressU) | (static_cast<std::uint64_t>(AddressV) << 8) |
               (static_cast<std::uint64_t>(MagFilter) << 16) | (static_cast<std::uint64_t>(MinFilter) << 24) |
               (static_cast<std::uint64_t>(MipFilter) << 32) | (static_cast<std::uint64_t>(MaxMipLevel) << 40) |
               (static_cast<std::uint64_t>(MaxAnisotropy) << 48);
    }
};

// A texture image for the GPU: every mip level, largest first, tightly packed. Bgra8: 4 bytes a
// pixel, B, G, R, A. Bc1 / Bc2 / Bc3: DXT1, DXT2 or DXT3, DXT4 or DXT5 blocks as the game stores
// them, uploaded as they are.
enum class ERpGpuTextureFormat : std::uint8_t
{
    Bgra8,
    Bc1,
    Bc2,
    Bc3,
};

struct FRpGpuTextureImage
{
    ERpGpuTextureFormat Format = ERpGpuTextureFormat::Bgra8;
    std::uint32_t Width = 0; // level 0
    std::uint32_t Height = 0;
    // A cube map: Levels holds six faces (+X, -X, +Y, -Y, +Z, -Z), each with its whole chain.
    bool bCube = false;
    std::vector<std::vector<std::uint8_t>> Levels;
};

struct FRpGpuCommand
{
    enum class EKind : std::uint8_t
    {
        Clear,
        Draw,
    };
    EKind Kind = EKind::Draw;

    // Clear: what to clear, and to what.
    bool bClearColor = false;
    bool bClearDepth = false;
    std::uint32_t ClearArgb = 0;
    float ClearDepth = 1.0f;

    // Draw: the pipeline, each stage's sampler and texture image (0: none), the geometry, and the
    // uniforms.
    // The backend fills Transform's Scale and Mode.x from the viewport; Lighting indexes the
    // frame's lighting blocks (-1: none, the draw is unlit).
    FRpGpuPipelineKey Pipeline;
    FRpGpuSamplerKey Sampler[kRpGpuStages];
    std::uint64_t Texture[kRpGpuStages] = {}; // per stage
    std::uint32_t FirstVertex = 0;
    std::uint32_t VertexCount = 0;
    std::uint32_t FirstIndex = 0;
    std::uint32_t IndexCount = 0; // 0: not indexed
    int Viewport[4] = {};         // x, y, width, height in pixels
    float MinZ = 0.0f;
    float MaxZ = 1.0f;
    FRpGpuTransformUniforms Transform = {};
    FRpGpuStageUniforms Stage = {};
    std::int32_t Lighting = -1;
    std::int32_t Blend = -1; // the frame's blend block, -1 when the draw does not blend
    // A vertex block kept on the GPU (UploadVertexBlock) the draw reads instead of the frame's
    // vertices; FirstVertex then counts from its start. 0: the frame's vertices.
    std::uint64_t VertexBlock = 0;
};

struct FRpGpuFrameRecording
{
    std::vector<FRpGpuVertex> Vertices;
    std::vector<std::uint16_t> Indices;
    std::vector<FRpGpuCommand> Commands;
    std::vector<FRpGpuLightingUniforms> Lighting;
    std::vector<FRpGpuBlendUniforms> Blend;

    void Clear()
    {
        Vertices.clear();
        Indices.clear();
        Commands.clear();
        Lighting.clear();
        Blend.clear();
    }
};

class IRpGpuBackend
{
public:
    virtual ~IRpGpuBackend() = default;

    // Brings the device and swap chain up on Hwnd, the client's window. Width and Height are the
    // size the client asked for; a backend with a real window uses the window's own size. A
    // second call with the same window keeps the device and only applies VSync.
    virtual bool Create(RpWindowHandle Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync) = 0;
    virtual void Destroy() = 0;
    virtual bool IsReady() const = 0;

    // The colour (D3DCOLOR, 0xAARRGGBB) the next frame is cleared to.
    virtual void SetClearColor(std::uint32_t Argb) = 0;

    // Draws Frame (clears and draws, in order) and presents it. A frame that does not start with
    // a clear starts from the SetClearColor colour. False when the device is missing or the frame
    // could not be submitted; the next Create then rebuilds the device.
    virtual bool Present(const FRpGpuFrameRecording& Frame) = 0;

    // A texture image, uploaded before the next frame's draws. Key names this image version
    // (never 0) and is never reused: when a texture changes, RPGPU uploads a new version under a
    // new key, so draws recorded earlier in the frame keep the image they saw.
    virtual void UploadTexture(std::uint64_t Key, FRpGpuTextureImage&& Image) = 0;
    // Whether UploadTexture takes Bc1 / Bc2 / Bc3 images; when it does not, RPGPU converts DXT
    // textures to Bgra8 first. Asked after Create.
    virtual bool SupportsBlockCompression() const = 0;
    // The image version of Key is no longer needed. RPGPU calls this only after the frame that
    // last used it has been presented.
    virtual void ForgetTexture(std::uint64_t Key) = 0;

    // A static vertex buffer's converted vertices, kept on the GPU under Key (never 0, never
    // reused) until ForgetVertexBlock, uploaded before the next frame's draws. Draws name it in
    // FRpGpuCommand::VertexBlock. ForgetVertexBlock comes only after the frame that last used
    // the block has been presented. False when the block could not be queued (memory): RPGPU
    // then draws from the frame's vertices instead. No exception leaves either call.
    virtual bool UploadVertexBlock(std::uint64_t Key, std::vector<FRpGpuVertex>&& Vertices) = 0;
    virtual void ForgetVertexBlock(std::uint64_t Key) = 0;
    // True when Key's block was dropped because the device could not make its buffer: RPGPU
    // forgets it and makes it again later.
    virtual bool IsVertexBlockLost(std::uint64_t Key) const = 0;

    virtual RpWindowHandle Hwnd() const = 0;
    virtual std::uint32_t Width() const = 0;
    virtual std::uint32_t Height() const = 0;

    // The full-screen display modes, largest first. False when they could not be queried (asked
    // again later).
    virtual bool QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out) = 0;

    // The last presented frame. False when the backend cannot read frames back.
    virtual bool ReadBack(FRpGpuFrame* Out) = 0;

    // The text of the last failure, for the module's log.
    virtual const char* LastError() const = 0;
};

std::unique_ptr<IRpGpuBackend> CreateRpGpuSdlBackend();
std::unique_ptr<IRpGpuBackend> CreateRpGpuNullBackend();
