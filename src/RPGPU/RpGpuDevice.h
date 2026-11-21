// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU's SDL3 side (port-only, docs/re/rpgpu.md): an SDL_GPU device and swap chain on the
// client's own window. The rest of RPGPU talks to the game through RP8's interfaces and never
// sees an SDL type; this class is the only place that includes SDL.

#include <windows.h>

#include <cstdint>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;

struct FRpGpuDisplayMode
{
    unsigned int Width;
    unsigned int Height;
    unsigned int Refresh;
};

class FRpGpuDevice
{
public:
    FRpGpuDevice() = default;
    ~FRpGpuDevice();

    FRpGpuDevice(const FRpGpuDevice&) = delete;
    FRpGpuDevice& operator=(const FRpGpuDevice&) = delete;

    // Wraps Hwnd in an SDL window, creates the GPU device and claims the window for it. The
    // window stays the client's: SDL neither creates nor destroys it. A second call with the
    // same window keeps the device and only applies VSync.
    bool Create(HWND Hwnd, bool bVSync);
    void Destroy();
    bool IsReady() const { return Device_ != nullptr; }

    // The colour (D3DCOLOR, 0xAARRGGBB) the next frame's swap-chain image is cleared to.
    void SetClearColor(std::uint32_t Argb) { ClearArgb_ = Argb; }

    // Clears the swap-chain image and presents it. False when the device is missing or the
    // frame could not be submitted; a minimised window (no swap-chain image) is not a failure. A
    // failed frame marks the device for a rebuild on the next Create.
    bool Present();

    HWND Hwnd() const { return Hwnd_; }
    std::uint32_t Width() const { return Width_; }
    std::uint32_t Height() const { return Height_; }

    // The primary display's full-screen modes, largest first (SDL's order). False when SDL's
    // video subsystem or the display could not be queried.
    static bool QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out);

    // The SDL error text of the last failure, for the module's log.
    const char* LastError() const;

private:
    HWND Hwnd_ = nullptr;
    SDL_Window* Window_ = nullptr;
    SDL_GPUDevice* Device_ = nullptr;
    bool bSdlVideo_ = false;
    bool bNeedsRebuild_ = false;
    // A failed swap-chain reconfiguration leaves SDL 3.4.18's D3D12 backend with records it has
    // already freed, and releasing the window would free them again. Such a device and window
    // are abandoned, not released, and the module stops creating devices: the flag is shared by
    // every instance, so at most one device is ever leaked.
    static inline bool bAbandoned_ = false;
    std::uint32_t ClearArgb_ = 0xFF000000u;
    std::uint32_t Width_ = 0;
    std::uint32_t Height_ = 0;
};
