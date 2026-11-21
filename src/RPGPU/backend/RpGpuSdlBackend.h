// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU's SDL backend (port-only, docs/re/rpgpu.md): an SDL_GPU device and swap chain on the
// client's own window. The rest of RPGPU talks to it through IRpGpuBackend and never sees an
// SDL type; this class is the only place that includes SDL.

#include "RpGpuBackend.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;

class FRpGpuSdlBackend final : public IRpGpuBackend
{
public:
    FRpGpuSdlBackend() = default;
    ~FRpGpuSdlBackend() override;

    FRpGpuSdlBackend(const FRpGpuSdlBackend&) = delete;
    FRpGpuSdlBackend& operator=(const FRpGpuSdlBackend&) = delete;

    // Wraps Hwnd in an SDL window, creates the GPU device and claims the window for it. The
    // window stays the client's: SDL neither creates nor destroys it, and its size is the
    // swap chain's (Width and Height are not used). A second call with the same window keeps the
    // device and only applies VSync.
    bool Create(RpWindowHandle Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync) override;
    void Destroy() override;
    bool IsReady() const override { return Device_ != nullptr; }

    void SetClearColor(std::uint32_t Argb) override { ClearArgb_ = Argb; }

    // Uploads the frame's textures and geometry in one copy pass, then replays its clears and
    // draws into the swap-chain image (one render pass per clear) and presents it. A minimised
    // window (no swap-chain image) is not a failure. A failed frame marks the device for a
    // rebuild on the next Create.
    bool Present(const FRpGpuFrameRecording& Frame) override;

    void UploadTexture(const void* Key, std::uint32_t Width, std::uint32_t Height,
                       const std::uint8_t* Bgra) override;
    void ForgetTexture(const void* Key) override;

    RpWindowHandle Hwnd() const override { return Hwnd_; }
    std::uint32_t Width() const override { return Width_; }
    std::uint32_t Height() const override { return Height_; }

    // The primary display's full-screen modes, largest first (SDL's order). False when SDL's
    // video subsystem or the display could not be queried.
    bool QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out) override;

    // Not yet: the swap-chain image cannot be read back. Frames become readable once RPGPU
    // renders into its own target (plan step 5.3).
    bool ReadBack(FRpGpuFrame* Out) override;

    // The SDL error text of the last failure.
    const char* LastError() const override;

private:
    struct FTexture
    {
        SDL_GPUTexture* Texture = nullptr;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
    };
    struct FPendingUpload
    {
        const void* Key;
        std::uint32_t Width;
        std::uint32_t Height;
        std::vector<std::uint8_t> Bgra;
    };

    bool CreateShaders();
    void ReleaseDeviceObjects();
    SDL_GPUGraphicsPipeline* Pipeline(const FRpGpuPipelineKey& Key);
    SDL_GPUSampler* Sampler(const FRpGpuSamplerKey& Key);
    bool EnsureDepth(std::uint32_t Width, std::uint32_t Height);
    bool EnsureBuffer(SDL_GPUBuffer** Buffer, std::uint32_t* Capacity, std::uint32_t Size, bool bIndex);
    bool UploadFrame(SDL_GPUCommandBuffer* Commands, const FRpGpuFrameRecording& Frame);
    SDL_GPURenderPass* BeginPass(SDL_GPUCommandBuffer* Commands, SDL_GPUTexture* Image, const FRpGpuCommand* Clear,
                                 bool bFirst);

    RpWindowHandle Hwnd_ = nullptr;
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

    // Device objects, made once per device (ReleaseDeviceObjects drops them with it).
    SDL_GPUShader* VertexShader_ = nullptr;
    SDL_GPUShader* FragmentShader_ = nullptr;
    std::uint32_t SwapchainFormat_ = 0; // SDL_GPUTextureFormat the pipelines were made for
    std::uint32_t DepthFormat_ = 0;
    std::unordered_map<std::uint64_t, SDL_GPUGraphicsPipeline*> Pipelines_;
    std::unordered_map<std::uint64_t, SDL_GPUSampler*> Samplers_;
    std::unordered_map<const void*, FTexture> Textures_;
    std::vector<FPendingUpload> PendingUploads_;
    SDL_GPUTexture* White_ = nullptr; // bound when a draw has no texture
    bool bWhiteUploaded_ = false;
    SDL_GPUTexture* Depth_ = nullptr;
    std::uint32_t DepthWidth_ = 0;
    std::uint32_t DepthHeight_ = 0;
    SDL_GPUBuffer* VertexBuffer_ = nullptr;
    std::uint32_t VertexCapacity_ = 0; // bytes
    SDL_GPUBuffer* IndexBuffer_ = nullptr;
    std::uint32_t IndexCapacity_ = 0; // bytes
    SDL_GPUTransferBuffer* Transfer_ = nullptr;
    std::uint32_t TransferCapacity_ = 0; // bytes
    std::uint32_t Width_ = 0;
    std::uint32_t Height_ = 0;
};
