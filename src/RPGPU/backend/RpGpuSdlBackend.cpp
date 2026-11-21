// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuSdlBackend.h"

#include <SDL3/SDL.h>

FRpGpuSdlBackend::~FRpGpuSdlBackend()
{
    Destroy();
}

bool FRpGpuSdlBackend::Create(RpWindowHandle Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync)
{
    (void)Width;
    (void)Height;
    if (Hwnd == nullptr || bAbandoned_)
        return false;
    if (Device_ != nullptr && (Hwnd_ != Hwnd || bNeedsRebuild_))
        Destroy();
    bNeedsRebuild_ = false;

    if (Device_ == nullptr)
    {
        if (!bSdlVideo_)
        {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                return false;
            bSdlVideo_ = true;
        }

        // The client's window, not a new one (SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER).
        const SDL_PropertiesID Props = SDL_CreateProperties();
        SDL_SetPointerProperty(Props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, Hwnd);
        Window_ = SDL_CreateWindowWithProperties(Props);
        SDL_DestroyProperties(Props);
        if (Window_ == nullptr)
            return false;

        // Every shader format SDL_GPU knows: the device picks its backend (Direct3D 12, Vulkan
        // or Metal) from what the system offers. The skeleton has no shaders yet.
        const SDL_GPUShaderFormat Formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                            SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_MSL;
#if defined(_DEBUG)
        const bool bDebug = true;
#else
        const bool bDebug = false;
#endif
        Device_ = SDL_CreateGPUDevice(Formats, bDebug, nullptr);
        if (Device_ == nullptr)
        {
            SDL_DestroyWindow(Window_);
            Window_ = nullptr;
            return false;
        }
        if (!SDL_ClaimWindowForGPUDevice(Device_, Window_))
        {
            SDL_DestroyGPUDevice(Device_);
            Device_ = nullptr;
            SDL_DestroyWindow(Window_);
            Window_ = nullptr;
            return false;
        }
        Hwnd_ = Hwnd;
    }

    // VSync is always supported; an unsupported immediate mode falls back to it. A failed
    // reconfiguration is a failed creation: the backend may have dropped the old swap chain.
    SDL_GPUPresentMode Mode = bVSync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!SDL_WindowSupportsGPUPresentMode(Device_, Window_, Mode))
        Mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!SDL_SetGPUSwapchainParameters(Device_, Window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, Mode))
    {
        // Leaked on purpose: see bAbandoned_. Rare, and better than freeing twice.
        Device_ = nullptr;
        Window_ = nullptr;
        bAbandoned_ = true;
        Hwnd_ = nullptr;
        return false;
    }

    int W = 0;
    int H = 0;
    if (SDL_GetWindowSizeInPixels(Window_, &W, &H))
    {
        Width_ = static_cast<std::uint32_t>(W);
        Height_ = static_cast<std::uint32_t>(H);
    }
    return true;
}

void FRpGpuSdlBackend::Destroy()
{
    if (Device_ != nullptr)
    {
        SDL_WaitForGPUIdle(Device_);
        if (Window_ != nullptr)
            SDL_ReleaseWindowFromGPUDevice(Device_, Window_);
        SDL_DestroyGPUDevice(Device_);
        Device_ = nullptr;
    }
    if (Window_ != nullptr)
    {
        // A window made from an existing HWND: SDL lets go of it and leaves it to the client.
        SDL_DestroyWindow(Window_);
        Window_ = nullptr;
    }
    // After an abandoned device the video subsystem stays up: quitting it would tear down the
    // leaked window.
    if (bSdlVideo_ && !bAbandoned_)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        bSdlVideo_ = false;
    }
    Hwnd_ = nullptr;
    Width_ = 0;
    Height_ = 0;
}

bool FRpGpuSdlBackend::Present()
{
    if (Device_ == nullptr)
        return false;
    SDL_GPUCommandBuffer* const Commands = SDL_AcquireGPUCommandBuffer(Device_);
    if (Commands == nullptr)
    {
        bNeedsRebuild_ = true;
        return false;
    }

    SDL_GPUTexture* Image = nullptr;
    Uint32 W = 0;
    Uint32 H = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(Commands, Window_, &Image, &W, &H))
    {
        SDL_CancelGPUCommandBuffer(Commands);
        bNeedsRebuild_ = true;
        return false;
    }
    if (Image != nullptr)
    {
        Width_ = W;
        Height_ = H;
        SDL_GPUColorTargetInfo Target = {};
        Target.texture = Image;
        Target.clear_color.r = static_cast<float>((ClearArgb_ >> 16) & 0xFF) / 255.0f;
        Target.clear_color.g = static_cast<float>((ClearArgb_ >> 8) & 0xFF) / 255.0f;
        Target.clear_color.b = static_cast<float>(ClearArgb_ & 0xFF) / 255.0f;
        Target.clear_color.a = static_cast<float>((ClearArgb_ >> 24) & 0xFF) / 255.0f;
        Target.load_op = SDL_GPU_LOADOP_CLEAR;
        Target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* const Pass = SDL_BeginGPURenderPass(Commands, &Target, 1, nullptr);
        if (Pass != nullptr)
            SDL_EndGPURenderPass(Pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(Commands))
    {
        bNeedsRebuild_ = true;
        return false;
    }
    return true;
}

bool FRpGpuSdlBackend::QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out)
{
    Out->clear();
    // The video subsystem is reference-counted: this works before and after Create.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return false;
    int Count = 0;
    SDL_DisplayMode** const Modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &Count);
    const bool bOk = Modes != nullptr;
    if (Modes != nullptr)
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            const SDL_DisplayMode* const Mode = Modes[Index];
            FRpGpuDisplayMode Entry;
            Entry.Width = static_cast<unsigned int>(Mode->w);
            Entry.Height = static_cast<unsigned int>(Mode->h);
            Entry.Refresh = static_cast<unsigned int>(Mode->refresh_rate + 0.5f);
            Out->push_back(Entry);
        }
        SDL_free(Modes);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return bOk;
}

bool FRpGpuSdlBackend::ReadBack(FRpGpuFrame* Out)
{
    (void)Out;
    return false;
}

const char* FRpGpuSdlBackend::LastError() const
{
    return SDL_GetError();
}

std::unique_ptr<IRpGpuBackend> CreateRpGpuSdlBackend()
{
    return std::make_unique<FRpGpuSdlBackend>();
}
