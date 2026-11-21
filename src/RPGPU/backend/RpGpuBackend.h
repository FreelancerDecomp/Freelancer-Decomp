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

// A pre-transformed vertex (D3DFVF_XYZRHW), whatever the game's own layout was: position in
// pixels, z 0..1 and 1 / w; the diffuse colour as D3DCOLOR (B, G, R, A bytes); texture set 0.
struct FRpGpuVertex2D
{
    float X;
    float Y;
    float Z;
    float Rhw;
    std::uint32_t Diffuse;
    float U;
    float V;
};
static_assert(sizeof(FRpGpuVertex2D) == 28, "the vertex layout the shaders read");

enum class ERpGpuTopology : std::uint8_t
{
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
};

// Everything a GPU pipeline object depends on, as Direct3D 8 numbers: render states 7 (ZENABLE),
// 14 (ZWRITEENABLE), 23 (ZFUNC), 27 (ALPHABLENDENABLE), 19 / 20 (SRCBLEND / DESTBLEND) and 0xA8
// (COLORWRITEENABLE).
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

    std::uint64_t Packed() const
    {
        return static_cast<std::uint64_t>(Topology) | (static_cast<std::uint64_t>(bZEnable) << 8) |
               (static_cast<std::uint64_t>(bZWrite) << 16) | (static_cast<std::uint64_t>(ZFunc) << 24) |
               (static_cast<std::uint64_t>(bBlend) << 32) | (static_cast<std::uint64_t>(SrcBlend) << 40) |
               (static_cast<std::uint64_t>(DstBlend) << 48) | (static_cast<std::uint64_t>(ColorWriteMask) << 56);
    }
};

// Texture stage 0's sampler, as Direct3D 8 numbers: ADDRESSU / ADDRESSV (D3DTEXTUREADDRESS) and
// MAGFILTER / MINFILTER / MIPFILTER (D3DTEXTUREFILTERTYPE).
struct FRpGpuSamplerKey
{
    std::uint8_t AddressU = 1;
    std::uint8_t AddressV = 1;
    std::uint8_t MagFilter = 2;
    std::uint8_t MinFilter = 2;
    std::uint8_t MipFilter = 0;

    std::uint64_t Packed() const
    {
        return static_cast<std::uint64_t>(AddressU) | (static_cast<std::uint64_t>(AddressV) << 8) |
               (static_cast<std::uint64_t>(MagFilter) << 16) | (static_cast<std::uint64_t>(MinFilter) << 24) |
               (static_cast<std::uint64_t>(MipFilter) << 32);
    }
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

    // Draw: the pipeline and sampler, the texture (null: none), the geometry, and the uniforms.
    FRpGpuPipelineKey Pipeline;
    FRpGpuSamplerKey Sampler;
    const void* Texture = nullptr;
    std::uint32_t FirstVertex = 0;
    std::uint32_t VertexCount = 0;
    std::uint32_t FirstIndex = 0;
    std::uint32_t IndexCount = 0; // 0: not indexed
    int Viewport[4] = {};         // x, y, width, height in pixels
    float MinZ = 0.0f;
    float MaxZ = 1.0f;
    std::uint32_t Ops[4] = {};       // rpgpu_stage0.frag's FStage.Ops
    std::uint32_t AlphaTest[4] = {}; // rpgpu_stage0.frag's FStage.AlphaTest
};

struct FRpGpuFrameRecording
{
    std::vector<FRpGpuVertex2D> Vertices;
    std::vector<std::uint16_t> Indices;
    std::vector<FRpGpuCommand> Commands;

    void Clear()
    {
        Vertices.clear();
        Indices.clear();
        Commands.clear();
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

    // A texture's level 0 as B, G, R, A bytes (Width x Height, no padding), uploaded before the
    // next frame's draws. Key is RPGPU's texture handle; a second upload replaces the first.
    virtual void UploadTexture(const void* Key, std::uint32_t Width, std::uint32_t Height,
                               const std::uint8_t* Bgra) = 0;
    // The texture of Key is gone: its GPU copy is released.
    virtual void ForgetTexture(const void* Key) = 0;

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
