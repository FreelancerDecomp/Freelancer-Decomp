// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// The seam between RPGPU's pipeline (RpGpu.*, which talks to the game through RP8's interfaces)
// and what puts pixels on a screen (docs/re/rpgpu.md). Two backends:
//
// - SDL (RpGpuDevice.*): an SDL_GPU device and swap chain on the client's window. The only code
//   that includes SDL.
// - Null (RpGpuNullBackend.*): no device. It accepts every call, reports a fixed list of display
//   modes and presents nothing, so RPGPU runs where there is no GPU: the CI smoke test and
//   screenshot comparison (x64-client-plan.md, steps 3.2 and 4.2). Selected with
//   [RenderPipeline] NULL_DEVICE = 1.

#include <windows.h>

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

class IRpGpuBackend
{
public:
    virtual ~IRpGpuBackend() = default;

    // Brings the device and swap chain up on Hwnd, the client's window. Width and Height are the
    // size the client asked for; a backend with a real window uses the window's own size. A
    // second call with the same window keeps the device and only applies VSync.
    virtual bool Create(HWND Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync) = 0;
    virtual void Destroy() = 0;
    virtual bool IsReady() const = 0;

    // The colour (D3DCOLOR, 0xAARRGGBB) the next frame is cleared to.
    virtual void SetClearColor(std::uint32_t Argb) = 0;

    // Clears the frame and presents it. False when the device is missing or the frame could not
    // be submitted; the next Create then rebuilds the device.
    virtual bool Present() = 0;

    virtual HWND Hwnd() const = 0;
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
