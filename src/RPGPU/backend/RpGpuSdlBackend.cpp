// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuSdlBackend.h"

#include "../shaders/RpGpuShaders.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace
{
    // Direct3D 8 numbers to SDL_GPU's.
    SDL_GPUBlendFactor BlendFactor(std::uint8_t D3dBlend)
    {
        switch (D3dBlend)
        {
        case 1: return SDL_GPU_BLENDFACTOR_ZERO;
        case 2: return SDL_GPU_BLENDFACTOR_ONE;
        case 3: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case 4: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case 5: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case 6: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case 7: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case 8: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case 9: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case 10: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case 11: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
        default: return SDL_GPU_BLENDFACTOR_ONE;
        }
    }

    SDL_GPUCompareOp CompareOp(std::uint8_t D3dCmp)
    {
        switch (D3dCmp)
        {
        case 1: return SDL_GPU_COMPAREOP_NEVER;
        case 2: return SDL_GPU_COMPAREOP_LESS;
        case 3: return SDL_GPU_COMPAREOP_EQUAL;
        case 4: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        case 5: return SDL_GPU_COMPAREOP_GREATER;
        case 6: return SDL_GPU_COMPAREOP_NOT_EQUAL;
        case 7: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        default: return SDL_GPU_COMPAREOP_ALWAYS;
        }
    }

    SDL_GPUSamplerAddressMode AddressMode(std::uint8_t D3dAddress)
    {
        switch (D3dAddress)
        {
        case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        case 3: // CLAMP
        case 4: // BORDER: SDL_GPU has no border colour
        case 5: // MIRRORONCE
            return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        default: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        }
    }

    SDL_GPUPrimitiveType PrimitiveType(ERpGpuTopology Topology)
    {
        switch (Topology)
        {
        case ERpGpuTopology::TriangleStrip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        case ERpGpuTopology::Lines: return SDL_GPU_PRIMITIVETYPE_LINELIST;
        case ERpGpuTopology::LineStrip: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
        default: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        }
    }

    SDL_FColor ToFColor(std::uint32_t Argb)
    {
        SDL_FColor Color;
        Color.r = static_cast<float>((Argb >> 16) & 0xFF) / 255.0f;
        Color.g = static_cast<float>((Argb >> 8) & 0xFF) / 255.0f;
        Color.b = static_cast<float>(Argb & 0xFF) / 255.0f;
        Color.a = static_cast<float>((Argb >> 24) & 0xFF) / 255.0f;
        return Color;
    }

    // rpgpu_pretransformed.vert's FScreen and rpgpu_stage0.frag's FStage.
    struct FScreenUniforms
    {
        float Scale[4];
        float Offset[4];
    };
    struct FStageUniforms
    {
        std::uint32_t Ops[4];
        std::uint32_t AlphaTest[4];
    };
}

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

        // The shader formats RPGPU ships (tools/rpgpu_shaders.ps1): the device picks its backend
        // (Direct3D 12, Vulkan or Metal) among those. No DXBC, so a Direct3D 12 device without
        // DXIL is not chosen only to fail on the first frame.
        const SDL_GPUShaderFormat Formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                            SDL_GPU_SHADERFORMAT_MSL;
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

void FRpGpuSdlBackend::ReleaseDeviceObjects()
{
    if (Device_ == nullptr)
        return;
    for (auto& Entry : Pipelines_)
        SDL_ReleaseGPUGraphicsPipeline(Device_, Entry.second);
    Pipelines_.clear();
    for (auto& Entry : Samplers_)
        SDL_ReleaseGPUSampler(Device_, Entry.second);
    Samplers_.clear();
    for (auto& Entry : Textures_)
        SDL_ReleaseGPUTexture(Device_, Entry.second.Texture);
    Textures_.clear();
    if (VertexShader_ != nullptr)
        SDL_ReleaseGPUShader(Device_, VertexShader_);
    if (FragmentShader_ != nullptr)
        SDL_ReleaseGPUShader(Device_, FragmentShader_);
    if (White_ != nullptr)
        SDL_ReleaseGPUTexture(Device_, White_);
    if (Depth_ != nullptr)
        SDL_ReleaseGPUTexture(Device_, Depth_);
    if (VertexBuffer_ != nullptr)
        SDL_ReleaseGPUBuffer(Device_, VertexBuffer_);
    if (IndexBuffer_ != nullptr)
        SDL_ReleaseGPUBuffer(Device_, IndexBuffer_);
    if (Transfer_ != nullptr)
        SDL_ReleaseGPUTransferBuffer(Device_, Transfer_);
    VertexShader_ = nullptr;
    FragmentShader_ = nullptr;
    White_ = nullptr;
    bWhiteUploaded_ = false;
    Depth_ = nullptr;
    DepthWidth_ = 0;
    DepthHeight_ = 0;
    VertexBuffer_ = nullptr;
    VertexCapacity_ = 0;
    IndexBuffer_ = nullptr;
    IndexCapacity_ = 0;
    Transfer_ = nullptr;
    TransferCapacity_ = 0;
    SwapchainFormat_ = 0;
    DepthFormat_ = 0; // probed again on the next device
    // Textures the game still holds come back through UploadTexture: RPGPU uploads a texture
    // again whenever its GPU copy is missing (docs/re/rpgpu.md, "Drawing").
}

void FRpGpuSdlBackend::Destroy()
{
    if (Device_ != nullptr)
    {
        SDL_WaitForGPUIdle(Device_);
        ReleaseDeviceObjects();
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

bool FRpGpuSdlBackend::CreateShaders()
{
    if (VertexShader_ != nullptr && FragmentShader_ != nullptr)
        return true;
    const FRpGpuShaderBlob* const Vert = FindRpGpuShader("rpgpu_pretransformed.vert");
    const FRpGpuShaderBlob* const Frag = FindRpGpuShader("rpgpu_stage0.frag");
    if (Vert == nullptr || Frag == nullptr)
        return SDL_SetError("RPGPU: shader bytecode missing");

    // The format the device takes: DXIL (Direct3D 12), SPIR-V (Vulkan) or MSL (Metal, whose entry
    // point SPIRV-Cross renames to main0).
    const SDL_GPUShaderFormat Formats = SDL_GetGPUShaderFormats(Device_);
    SDL_GPUShaderFormat Format = SDL_GPU_SHADERFORMAT_INVALID;
    if ((Formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
        Format = SDL_GPU_SHADERFORMAT_DXIL;
    else if ((Formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
        Format = SDL_GPU_SHADERFORMAT_SPIRV;
    else if ((Formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
        Format = SDL_GPU_SHADERFORMAT_MSL;
    else
        return SDL_SetError("RPGPU: no shader format the device takes");

    auto Make = [&](const FRpGpuShaderBlob* Blob, SDL_GPUShaderStage Stage, Uint32 Samplers) -> SDL_GPUShader* {
        SDL_GPUShaderCreateInfo Info = {};
        if (Format == SDL_GPU_SHADERFORMAT_DXIL)
        {
            Info.code = Blob->Dxil;
            Info.code_size = Blob->DxilSize;
            Info.entrypoint = "main";
        }
        else if (Format == SDL_GPU_SHADERFORMAT_SPIRV)
        {
            Info.code = Blob->Spirv;
            Info.code_size = Blob->SpirvSize;
            Info.entrypoint = "main";
        }
        else
        {
            Info.code = Blob->Msl;
            Info.code_size = Blob->MslSize;
            Info.entrypoint = "main0";
        }
        Info.format = Format;
        Info.stage = Stage;
        Info.num_samplers = Samplers;
        Info.num_uniform_buffers = 1;
        return SDL_CreateGPUShader(Device_, &Info);
    };
    VertexShader_ = Make(Vert, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    FragmentShader_ = Make(Frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
    return VertexShader_ != nullptr && FragmentShader_ != nullptr;
}

SDL_GPUGraphicsPipeline* FRpGpuSdlBackend::Pipeline(const FRpGpuPipelineKey& Key)
{
    const std::uint64_t Packed = Key.Packed();
    auto Found = Pipelines_.find(Packed);
    if (Found != Pipelines_.end())
        return Found->second;

    SDL_GPUVertexBufferDescription Buffer = {};
    Buffer.slot = 0;
    Buffer.pitch = sizeof(FRpGpuVertex2D);
    Buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute Attributes[3] = {};
    Attributes[0].location = 0;
    Attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    Attributes[0].offset = 0;
    Attributes[1].location = 1;
    Attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    Attributes[1].offset = 16;
    Attributes[2].location = 2;
    Attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    Attributes[2].offset = 20;

    SDL_GPUColorTargetDescription Color = {};
    Color.format = static_cast<SDL_GPUTextureFormat>(SwapchainFormat_);
    Color.blend_state.enable_blend = Key.bBlend != 0;
    Color.blend_state.src_color_blendfactor = BlendFactor(Key.SrcBlend);
    Color.blend_state.dst_color_blendfactor = BlendFactor(Key.DstBlend);
    Color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    Color.blend_state.src_alpha_blendfactor = BlendFactor(Key.SrcBlend);
    Color.blend_state.dst_alpha_blendfactor = BlendFactor(Key.DstBlend);
    Color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    Color.blend_state.enable_color_write_mask = true;
    Color.blend_state.color_write_mask = static_cast<SDL_GPUColorComponentFlags>(
        ((Key.ColorWriteMask & 1) != 0 ? SDL_GPU_COLORCOMPONENT_R : 0) |
        ((Key.ColorWriteMask & 2) != 0 ? SDL_GPU_COLORCOMPONENT_G : 0) |
        ((Key.ColorWriteMask & 4) != 0 ? SDL_GPU_COLORCOMPONENT_B : 0) |
        ((Key.ColorWriteMask & 8) != 0 ? SDL_GPU_COLORCOMPONENT_A : 0));

    SDL_GPUGraphicsPipelineCreateInfo Info = {};
    Info.vertex_shader = VertexShader_;
    Info.fragment_shader = FragmentShader_;
    Info.vertex_input_state.vertex_buffer_descriptions = &Buffer;
    Info.vertex_input_state.num_vertex_buffers = 1;
    Info.vertex_input_state.vertex_attributes = Attributes;
    Info.vertex_input_state.num_vertex_attributes = 3;
    Info.primitive_type = PrimitiveType(Key.Topology);
    Info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // Pre-transformed 2D draws do not rely on culling; CULLMODE arrives with the 3D draws.
    Info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    Info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    Info.depth_stencil_state.enable_depth_test = Key.bZEnable != 0;
    Info.depth_stencil_state.enable_depth_write = Key.bZEnable != 0 && Key.bZWrite != 0;
    Info.depth_stencil_state.compare_op = CompareOp(Key.ZFunc);
    Info.target_info.color_target_descriptions = &Color;
    Info.target_info.num_color_targets = 1;
    Info.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(DepthFormat_);
    Info.target_info.has_depth_stencil_target = true;

    SDL_GPUGraphicsPipeline* const Made = SDL_CreateGPUGraphicsPipeline(Device_, &Info);
    if (Made != nullptr)
        Pipelines_[Packed] = Made;
    return Made;
}

SDL_GPUSampler* FRpGpuSdlBackend::Sampler(const FRpGpuSamplerKey& Key)
{
    const std::uint64_t Packed = Key.Packed();
    auto Found = Samplers_.find(Packed);
    if (Found != Samplers_.end())
        return Found->second;
    SDL_GPUSamplerCreateInfo Info = {};
    Info.mag_filter = Key.MagFilter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    Info.min_filter = Key.MinFilter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    Info.mipmap_mode = Key.MipFilter == 2 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    Info.address_mode_u = AddressMode(Key.AddressU);
    Info.address_mode_v = AddressMode(Key.AddressV);
    Info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    // Textures carry level 0 only so far: no mip levels to choose from.
    Info.min_lod = 0.0f;
    Info.max_lod = 0.0f;
    SDL_GPUSampler* const Made = SDL_CreateGPUSampler(Device_, &Info);
    if (Made != nullptr)
        Samplers_[Packed] = Made;
    return Made;
}

bool FRpGpuSdlBackend::EnsureDepth(std::uint32_t Width, std::uint32_t Height)
{
    if (DepthFormat_ == 0)
    {
        const SDL_GPUTextureFormat Candidates[] = {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                                   SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                                   SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D16_UNORM};
        for (SDL_GPUTextureFormat Candidate : Candidates)
        {
            if (SDL_GPUTextureSupportsFormat(Device_, Candidate, SDL_GPU_TEXTURETYPE_2D,
                                             SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            {
                DepthFormat_ = static_cast<std::uint32_t>(Candidate);
                break;
            }
        }
        if (DepthFormat_ == 0)
            return SDL_SetError("RPGPU: no depth format");
    }
    if (Depth_ != nullptr && DepthWidth_ == Width && DepthHeight_ == Height)
        return true;
    if (Depth_ != nullptr)
        SDL_ReleaseGPUTexture(Device_, Depth_);
    SDL_GPUTextureCreateInfo Info = {};
    Info.type = SDL_GPU_TEXTURETYPE_2D;
    Info.format = static_cast<SDL_GPUTextureFormat>(DepthFormat_);
    Info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    Info.width = Width;
    Info.height = Height;
    Info.layer_count_or_depth = 1;
    Info.num_levels = 1;
    Depth_ = SDL_CreateGPUTexture(Device_, &Info);
    DepthWidth_ = Depth_ != nullptr ? Width : 0;
    DepthHeight_ = Depth_ != nullptr ? Height : 0;
    return Depth_ != nullptr;
}

bool FRpGpuSdlBackend::EnsureBuffer(SDL_GPUBuffer** Buffer, std::uint32_t* Capacity, std::uint32_t Size, bool bIndex)
{
    if (*Buffer != nullptr && *Capacity >= Size)
        return true;
    if (*Buffer != nullptr)
        SDL_ReleaseGPUBuffer(Device_, *Buffer);
    // Grown by half again, so a frame that grows a little does not reallocate every time.
    const std::uint32_t Grown = std::max<std::uint32_t>(Size, *Capacity + *Capacity / 2);
    SDL_GPUBufferCreateInfo Info = {};
    Info.usage = bIndex ? SDL_GPU_BUFFERUSAGE_INDEX : SDL_GPU_BUFFERUSAGE_VERTEX;
    Info.size = Grown;
    *Buffer = SDL_CreateGPUBuffer(Device_, &Info);
    *Capacity = *Buffer != nullptr ? Grown : 0;
    return *Buffer != nullptr;
}

void FRpGpuSdlBackend::UploadTexture(std::uint64_t Key, std::uint32_t Width, std::uint32_t Height,
                                     const std::uint8_t* Bgra)
{
    if (Key == 0 || Width == 0 || Height == 0 || Bgra == nullptr)
        return;
    FPendingUpload Upload;
    Upload.Key = Key;
    Upload.Width = Width;
    Upload.Height = Height;
    Upload.Bgra.assign(Bgra, Bgra + static_cast<std::size_t>(Width) * Height * 4);
    // A newer upload of the same texture replaces a pending older one.
    for (FPendingUpload& Pending : PendingUploads_)
    {
        if (Pending.Key == Key)
        {
            Pending = std::move(Upload);
            return;
        }
    }
    PendingUploads_.push_back(std::move(Upload));
}

void FRpGpuSdlBackend::ForgetTexture(std::uint64_t Key)
{
    PendingUploads_.erase(std::remove_if(PendingUploads_.begin(), PendingUploads_.end(),
                                         [Key](const FPendingUpload& Pending) { return Pending.Key == Key; }),
                          PendingUploads_.end());
    auto Found = Textures_.find(Key);
    if (Found == Textures_.end())
        return;
    if (Device_ != nullptr)
        SDL_ReleaseGPUTexture(Device_, Found->second.Texture);
    Textures_.erase(Found);
}

// One copy pass: pending textures (and the white stand-in), then the frame's vertices and
// indices, all through one transfer buffer.
bool FRpGpuSdlBackend::UploadFrame(SDL_GPUCommandBuffer* Commands, const FRpGpuFrameRecording& Frame)
{
    if (White_ == nullptr)
    {
        SDL_GPUTextureCreateInfo Info = {};
        Info.type = SDL_GPU_TEXTURETYPE_2D;
        Info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        Info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        Info.width = 1;
        Info.height = 1;
        Info.layer_count_or_depth = 1;
        Info.num_levels = 1;
        White_ = SDL_CreateGPUTexture(Device_, &Info);
        if (White_ == nullptr)
            return false;
    }
    // (Re)create the GPU textures of the pending uploads.
    for (const FPendingUpload& Upload : PendingUploads_)
    {
        FTexture& Entry = Textures_[Upload.Key];
        if (Entry.Texture != nullptr && (Entry.Width != Upload.Width || Entry.Height != Upload.Height))
        {
            SDL_ReleaseGPUTexture(Device_, Entry.Texture);
            Entry.Texture = nullptr;
        }
        if (Entry.Texture == nullptr)
        {
            SDL_GPUTextureCreateInfo Info = {};
            Info.type = SDL_GPU_TEXTURETYPE_2D;
            Info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            Info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            Info.width = Upload.Width;
            Info.height = Upload.Height;
            Info.layer_count_or_depth = 1;
            Info.num_levels = 1;
            Entry.Texture = SDL_CreateGPUTexture(Device_, &Info);
            Entry.Width = Upload.Width;
            Entry.Height = Upload.Height;
            if (Entry.Texture == nullptr)
            {
                Textures_.erase(Upload.Key);
                return false;
            }
        }
    }

    // Sizes in 64 bits first: SDL_GPU's buffers and offsets are 32-bit, so a frame whose uploads
    // add up to more than that is refused instead of wrapping.
    const std::uint64_t VertexBytes64 = static_cast<std::uint64_t>(Frame.Vertices.size()) * sizeof(FRpGpuVertex2D);
    const std::uint64_t IndexBytes64 = static_cast<std::uint64_t>(Frame.Indices.size()) * sizeof(std::uint16_t);
    std::uint64_t TextureBytes64 = bWhiteUploaded_ ? 0u : 4u;
    for (const FPendingUpload& Upload : PendingUploads_)
        TextureBytes64 += Upload.Bgra.size();
    const std::uint64_t Total64 = TextureBytes64 + VertexBytes64 + IndexBytes64;
    if (Total64 == 0)
        return true;
    if (Total64 > 0xFFFFFFFFull)
        return SDL_SetError("RPGPU: a frame's uploads exceed 4 GB");
    const std::uint32_t VertexBytes = static_cast<std::uint32_t>(VertexBytes64);
    const std::uint32_t IndexBytes = static_cast<std::uint32_t>(IndexBytes64);
    const std::uint32_t Total = static_cast<std::uint32_t>(Total64);
    if (VertexBytes != 0 && !EnsureBuffer(&VertexBuffer_, &VertexCapacity_, VertexBytes, false))
        return false;
    if (IndexBytes != 0 && !EnsureBuffer(&IndexBuffer_, &IndexCapacity_, IndexBytes, true))
        return false;
    if (Transfer_ == nullptr || TransferCapacity_ < Total)
    {
        if (Transfer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(Device_, Transfer_);
        SDL_GPUTransferBufferCreateInfo Info = {};
        Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        Info.size = std::max<std::uint32_t>(Total, TransferCapacity_ + TransferCapacity_ / 2);
        Transfer_ = SDL_CreateGPUTransferBuffer(Device_, &Info);
        TransferCapacity_ = Transfer_ != nullptr ? Info.size : 0;
        if (Transfer_ == nullptr)
            return false;
    }

    // cycle: the previous frame may still be reading the transfer buffer.
    std::uint8_t* const Mapped = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(Device_, Transfer_, true));
    if (Mapped == nullptr)
        return false;
    std::uint32_t Offset = 0;
    const std::uint32_t WhiteOffset = Offset;
    if (!bWhiteUploaded_)
    {
        const std::uint8_t White[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        std::memcpy(Mapped + Offset, White, 4);
        Offset += 4;
    }
    std::vector<std::uint32_t> UploadOffsets;
    for (const FPendingUpload& Upload : PendingUploads_)
    {
        UploadOffsets.push_back(Offset);
        std::memcpy(Mapped + Offset, Upload.Bgra.data(), Upload.Bgra.size());
        Offset += static_cast<std::uint32_t>(Upload.Bgra.size());
    }
    const std::uint32_t VertexOffset = Offset;
    if (VertexBytes != 0)
        std::memcpy(Mapped + Offset, Frame.Vertices.data(), VertexBytes);
    Offset += VertexBytes;
    const std::uint32_t IndexOffset = Offset;
    if (IndexBytes != 0)
        std::memcpy(Mapped + Offset, Frame.Indices.data(), IndexBytes);
    SDL_UnmapGPUTransferBuffer(Device_, Transfer_);

    SDL_GPUCopyPass* const Copy = SDL_BeginGPUCopyPass(Commands);
    if (Copy == nullptr)
        return false;
    auto UploadImage = [&](SDL_GPUTexture* Texture, std::uint32_t From, std::uint32_t W, std::uint32_t H) {
        SDL_GPUTextureTransferInfo Source = {};
        Source.transfer_buffer = Transfer_;
        Source.offset = From;
        Source.pixels_per_row = W;
        Source.rows_per_layer = H;
        SDL_GPUTextureRegion Target = {};
        Target.texture = Texture;
        Target.w = W;
        Target.h = H;
        Target.d = 1;
        SDL_UploadToGPUTexture(Copy, &Source, &Target, false);
    };
    if (!bWhiteUploaded_)
    {
        UploadImage(White_, WhiteOffset, 1, 1);
        bWhiteUploaded_ = true;
    }
    for (std::size_t Index = 0; Index < PendingUploads_.size(); ++Index)
    {
        const FPendingUpload& Upload = PendingUploads_[Index];
        UploadImage(Textures_[Upload.Key].Texture, UploadOffsets[Index], Upload.Width, Upload.Height);
    }
    PendingUploads_.clear();
    auto UploadBuffer = [&](SDL_GPUBuffer* Buffer, std::uint32_t From, std::uint32_t Size) {
        SDL_GPUTransferBufferLocation Source = {};
        Source.transfer_buffer = Transfer_;
        Source.offset = From;
        SDL_GPUBufferRegion Target = {};
        Target.buffer = Buffer;
        Target.offset = 0;
        Target.size = Size;
        SDL_UploadToGPUBuffer(Copy, &Source, &Target, true);
    };
    if (VertexBytes != 0)
        UploadBuffer(VertexBuffer_, VertexOffset, VertexBytes);
    if (IndexBytes != 0)
        UploadBuffer(IndexBuffer_, IndexOffset, IndexBytes);
    SDL_EndGPUCopyPass(Copy);
    return true;
}

// A render pass on the swap-chain image and the depth buffer. The first pass of a frame clears
// colour to the clear command's colour (or SetClearColor's) and depth to 1; a later pass clears
// only what its clear command asks for and keeps the rest.
SDL_GPURenderPass* FRpGpuSdlBackend::BeginPass(SDL_GPUCommandBuffer* Commands, SDL_GPUTexture* Image,
                                               const FRpGpuCommand* Clear, bool bFirst)
{
    SDL_GPUColorTargetInfo Color = {};
    Color.texture = Image;
    Color.store_op = SDL_GPU_STOREOP_STORE;
    const bool bClearColor = bFirst || (Clear != nullptr && Clear->bClearColor);
    Color.load_op = bClearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    Color.clear_color = ToFColor(Clear != nullptr && Clear->bClearColor ? Clear->ClearArgb : ClearArgb_);

    SDL_GPUDepthStencilTargetInfo Depth = {};
    Depth.texture = Depth_;
    const bool bClearDepth = bFirst || (Clear != nullptr && Clear->bClearDepth);
    Depth.load_op = bClearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    Depth.clear_depth = Clear != nullptr && Clear->bClearDepth ? Clear->ClearDepth : 1.0f;
    Depth.store_op = SDL_GPU_STOREOP_STORE;
    Depth.stencil_load_op = bFirst ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    Depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
    return SDL_BeginGPURenderPass(Commands, &Color, 1, &Depth);
}

bool FRpGpuSdlBackend::Present(const FRpGpuFrameRecording& Frame)
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
        const std::uint32_t Format = static_cast<std::uint32_t>(SDL_GetGPUSwapchainTextureFormat(Device_, Window_));
        if (Format != SwapchainFormat_)
        {
            // The pipelines are made for the swap chain's format.
            for (auto& Entry : Pipelines_)
                SDL_ReleaseGPUGraphicsPipeline(Device_, Entry.second);
            Pipelines_.clear();
            SwapchainFormat_ = Format;
        }
        bool bReady = CreateShaders() && EnsureDepth(W, H) && UploadFrame(Commands, Frame);
        // Every pipeline and sampler the frame needs, before any render pass starts: a draw that
        // cannot get one fails the frame rather than vanishing.
        for (std::size_t Index = 0; bReady && Index < Frame.Commands.size(); ++Index)
        {
            const FRpGpuCommand& Command = Frame.Commands[Index];
            if (Command.Kind == FRpGpuCommand::EKind::Draw && Command.VertexCount != 0)
                bReady = Pipeline(Command.Pipeline) != nullptr && Sampler(Command.Sampler) != nullptr;
        }
        if (!bReady)
        {
            // The swap-chain image is acquired, so the command buffer cannot be cancelled any
            // more: it is submitted with nothing drawn, and the device is rebuilt.
            SDL_SubmitGPUCommandBuffer(Commands);
            bNeedsRebuild_ = true;
            return false;
        }

        // A leading clear starts the first pass; every later clear starts a new one.
        std::size_t First = 0;
        const FRpGpuCommand* Lead = nullptr;
        if (!Frame.Commands.empty() && Frame.Commands[0].Kind == FRpGpuCommand::EKind::Clear)
        {
            Lead = &Frame.Commands[0];
            First = 1;
        }
        SDL_GPURenderPass* Pass = BeginPass(Commands, Image, Lead, true);
        for (std::size_t Index = First; Pass != nullptr && Index < Frame.Commands.size(); ++Index)
        {
            const FRpGpuCommand& Command = Frame.Commands[Index];
            if (Command.Kind == FRpGpuCommand::EKind::Clear)
            {
                SDL_EndGPURenderPass(Pass);
                Pass = BeginPass(Commands, Image, &Command, false);
                continue;
            }
            SDL_GPUGraphicsPipeline* const Bound = Pipeline(Command.Pipeline);
            SDL_GPUSampler* const Sampling = Sampler(Command.Sampler);
            if (Bound == nullptr || Sampling == nullptr || Command.VertexCount == 0)
                continue;
            SDL_BindGPUGraphicsPipeline(Pass, Bound);

            SDL_GPUViewport Viewport = {};
            Viewport.x = static_cast<float>(Command.Viewport[0]);
            Viewport.y = static_cast<float>(Command.Viewport[1]);
            Viewport.w = static_cast<float>(Command.Viewport[2] > 0 ? Command.Viewport[2] : static_cast<int>(W));
            Viewport.h = static_cast<float>(Command.Viewport[3] > 0 ? Command.Viewport[3] : static_cast<int>(H));
            Viewport.min_depth = Command.MinZ;
            Viewport.max_depth = Command.MaxZ;
            SDL_SetGPUViewport(Pass, &Viewport);

            SDL_GPUBufferBinding Vertices = {};
            Vertices.buffer = VertexBuffer_;
            Vertices.offset = 0;
            SDL_BindGPUVertexBuffers(Pass, 0, &Vertices, 1);

            auto Found = Textures_.find(Command.Texture);
            SDL_GPUTextureSamplerBinding Binding = {};
            Binding.texture = Found != Textures_.end() ? Found->second.Texture : White_;
            Binding.sampler = Sampling;
            SDL_BindGPUFragmentSamplers(Pass, 0, &Binding, 1);

            FScreenUniforms Screen = {};
            Screen.Scale[0] = 2.0f / Viewport.w;
            Screen.Scale[1] = -2.0f / Viewport.h;
            Screen.Scale[2] = Viewport.x;
            Screen.Scale[3] = Viewport.y;
            // Direct3D 8 puts pixel centres on integer coordinates, SDL_GPU's backends at +0.5.
            Screen.Offset[0] = 0.5f;
            SDL_PushGPUVertexUniformData(Commands, 0, &Screen, sizeof(Screen));
            FStageUniforms Stage = {};
            std::memcpy(Stage.Ops, Command.Ops, sizeof(Stage.Ops));
            std::memcpy(Stage.AlphaTest, Command.AlphaTest, sizeof(Stage.AlphaTest));
            if (Found == Textures_.end())
                Stage.Ops[2] = 0; // no GPU copy: as if no texture were bound
            SDL_PushGPUFragmentUniformData(Commands, 0, &Stage, sizeof(Stage));

            if (Command.IndexCount != 0)
            {
                SDL_GPUBufferBinding Indices = {};
                Indices.buffer = IndexBuffer_;
                Indices.offset = 0;
                SDL_BindGPUIndexBuffer(Pass, &Indices, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                SDL_DrawGPUIndexedPrimitives(Pass, Command.IndexCount, 1, Command.FirstIndex,
                                             static_cast<Sint32>(Command.FirstVertex), 0);
            }
            else
            {
                SDL_DrawGPUPrimitives(Pass, Command.VertexCount, 1, Command.FirstVertex, 0);
            }
        }
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
