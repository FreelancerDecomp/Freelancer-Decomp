// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU's SDL backend (port-only, docs/re/rpgpu.md): an SDL_GPU device and swap chain on the
// client's own window. The rest of RPGPU talks to it through IRpGpuBackend and never sees an
// SDL type; this class is the only place that includes SDL.

#include "RpGpuBackend.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

    void UploadTexture(std::uint64_t Key, FRpGpuTextureImage&& Image) override;
    bool SupportsBlockCompression() const override { return bBlockCompression_; }
    void ForgetTexture(std::uint64_t Key) override;
    bool UploadVertexBlock(std::uint64_t Key, std::vector<FRpGpuVertex>&& Vertices) override;
    bool UploadIndexBlock(std::uint64_t Key, std::vector<std::uint16_t>&& Indices) override;
    void ForgetBlock(std::uint64_t Key) override;
    bool IsBlockLost(std::uint64_t Key) const override { return LostBlocks_.count(Key) != 0; }

    RpWindowHandle Hwnd() const override { return Hwnd_; }
    std::uint32_t Width() const override { return Width_; }
    std::uint32_t Height() const override { return Height_; }

    // The primary display's full-screen modes, largest first (SDL's order). False when SDL's
    // video subsystem or the display could not be queried.
    bool QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out) override;

    // The last presented frame, downloaded from RPGPU's own colour target (the swap chain gets a
    // copy of it). Waits for the GPU: for screenshots, not every frame.
    bool ReadBack(FRpGpuFrame* Out) override;

    // The SDL error text of the last failure.
    const char* LastError() const override;

private:
    struct FTexture
    {
        SDL_GPUTexture* Texture = nullptr;
        ERpGpuTextureFormat Format = ERpGpuTextureFormat::Bgra8;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t Levels = 0; // per face
        bool bCube = false;
    };
    struct FPendingUpload
    {
        std::uint64_t Key;
        FRpGpuTextureImage Image;
    };
    struct FPendingBlock
    {
        std::uint64_t Key;
        std::vector<FRpGpuVertex> Vertices; // a vertex block, or
        std::vector<std::uint16_t> Indices; // an index block
        const void* Data() const { return Indices.empty() ? static_cast<const void*>(Vertices.data()) : Indices.data(); }
        std::size_t Bytes() const { return Vertices.size() * sizeof(FRpGpuVertex) + Indices.size() * 2; }
        // Its buffer could not be made and the loss could not be recorded (no memory): it stays
        // pending, out of this frame's upload, and the next frame tries again.
        bool bDeferred = false;
    };

    bool CreateShaders();
    void ReleaseDeviceObjects();
    SDL_GPUGraphicsPipeline* Pipeline(const FRpGpuPipelineKey& Key);
    SDL_GPUSampler* Sampler(const FRpGpuSamplerKey& Key);
    bool EnsureDepth(std::uint32_t Width, std::uint32_t Height);
    bool EnsureTarget(std::uint32_t Width, std::uint32_t Height);
    bool EnsureBuffer(SDL_GPUBuffer** Buffer, std::uint32_t* Capacity, std::uint32_t Size, bool bIndex);
    bool UploadFrame(SDL_GPUCommandBuffer* Commands, const FRpGpuFrameRecording& Frame);
    SDL_GPURenderPass* BeginPass(SDL_GPUCommandBuffer* Commands, SDL_GPUTexture* Image, const FRpGpuCommand* Clear,
                                 bool bFirst);

    RpWindowHandle Hwnd_ = nullptr;
    SDL_Window* Window_ = nullptr;
    SDL_GPUDevice* Device_ = nullptr;
    bool bSdlVideo_ = false;
    bool bNeedsRebuild_ = false;
    bool bBlockCompression_ = false; // the device samples BC1, BC2 and BC3
    // A failed swap-chain reconfiguration leaves SDL 3.4.18's D3D12 backend with records it has
    // already freed, and releasing the window would free them again. Such a device and window
    // are abandoned, not released, and the module stops creating devices: the flag is shared by
    // every instance, so at most one device is ever leaked.
    static inline bool bAbandoned_ = false;
    std::uint32_t ClearArgb_ = 0xFF000000u;

    // Device objects, made once per device (ReleaseDeviceObjects drops them with it).
    SDL_GPUShader* VertexShader_ = nullptr;
    SDL_GPUShader* FragmentShader_ = nullptr;
    std::uint32_t DepthFormat_ = 0;
    std::unordered_map<std::uint64_t, SDL_GPUGraphicsPipeline*> Pipelines_;
    // By FRpGpuSamplerKey: Packed() and the LOD bias's bits.
    std::map<std::pair<std::uint64_t, std::uint32_t>, SDL_GPUSampler*> Samplers_;
    std::unordered_map<std::uint64_t, FTexture> Textures_;
    std::vector<FPendingUpload> PendingUploads_;
    // Static vertex and index buffers kept on the GPU (UploadVertexBlock, UploadIndexBlock), one
    // GPU buffer each.
    std::unordered_map<std::uint64_t, SDL_GPUBuffer*> Blocks_;
    std::vector<FPendingBlock> PendingBlocks_;
    std::unordered_set<std::uint64_t> LostBlocks_; // blocks whose buffer could not be made
    SDL_GPUTexture* White_ = nullptr;     // bound when a draw has no texture
    SDL_GPUTexture* WhiteCube_ = nullptr; // bound to the cube slots when a stage has no cube map
    bool bWhiteUploaded_ = false;
    // The frame is drawn here, then copied to the swap-chain image, so it can be read back.
    SDL_GPUTexture* Target_ = nullptr;
    std::uint32_t TargetWidth_ = 0;
    std::uint32_t TargetHeight_ = 0;
    bool bHasFrame_ = false; // Target_ holds a presented frame
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
