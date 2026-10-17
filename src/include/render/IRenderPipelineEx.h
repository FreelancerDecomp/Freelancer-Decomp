// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only Direct3D 9 extensions of the render pipeline (RP9.dll). Retail has nothing like
// this: RP8.dll (Direct3D 8, the retail pipeline) does not answer these IIDs, so a consumer
// QueryInterfaces the pipeline for Iids::RenderPipelineEx / Iids::RPDrawEx and, on failure,
// keeps its old (fixed-function) algorithm unchanged. See docs/re/rp9.md, "Ex extensions".
//
// The API is generic GPU plumbing (programmable shaders, render targets, volume textures, the
// scene depth as a texture) -- it knows nothing about nebulae or particles. Every type here is
// D3D-version neutral, so D3D8-branch translation units (the exe) can include it.
//
// Lifetime rules:
// - Handles are opaque and owned by the pipeline. They survive device loss / Reset
//   (CreateBuffers): render targets and the scene depth are recreated behind the same handle.
// - Shader programs, render-target state, sampler bindings and render states changed inside
//   BeginExPass/EndExPass are pass-scoped: EndExPass puts back the colour target, depth surface,
//   viewport, fixed-function texture bindings/sampler states and every render state the pass
//   changed (through IRenderPipeline::SetRenderState as well), and unbinds the shaders.
// - Render states inside a pass are set with the ordinary IRenderPipeline::SetRenderState
//   (Direct3D 8 numbering, like everywhere else); they are restored at EndExPass.

#include "dacom.h"

#include <cstddef>

struct IFileSystem;

// Opaque handles.
struct FRPExProgram;
struct FRPExTexture;

// Texture formats an Ex texture can be created with.
enum ERPExFormat : unsigned int
{
    RPExFormatA8R8G8B8 = 1,
    RPExFormatL8 = 2,            // one 8-bit channel (volume noise)
    RPExFormatA16B16G16R16F = 3, // FP16 RGBA (HDR)
    RPExFormatR16F = 4,
    RPExFormatR32F = 5,
    RPExFormatG16R16F = 6,
};

enum ERPExFilter : unsigned int
{
    RPExFilterPoint = 0,
    RPExFilterLinear = 1,
};

enum ERPExMipFilter : unsigned int
{
    RPExMipNone = 0,
    RPExMipPoint = 1,
    RPExMipLinear = 2,
};

enum ERPExAddress : unsigned int
{
    RPExAddressWrap = 0,
    RPExAddressClamp = 1,
    RPExAddressMirror = 2,
};

// Sampler state a texture is bound with (always explicit; nothing is inherited).
struct FRPExSamplerState
{
    ERPExFilter MinMagFilter;
    ERPExMipFilter MipFilter;
    ERPExAddress AddressU;
    ERPExAddress AddressV;
    ERPExAddress AddressW;
};

// Size-versioned: the caller sets Size to the size of the struct it was compiled with (at least
// RPExCapsSizeV1); GetExCaps fills that many bytes (never more than its own sizeof) and writes
// back the size it filled. New fields are only ever appended.
struct FRPExCaps
{
    unsigned long Size;                // in: the caller's sizeof(FRPExCaps); out: bytes filled
    unsigned long VertexShaderVersion; // (major << 8) | minor, 0x300 = vs_3_0
    unsigned long PixelShaderVersion;  // (major << 8) | minor, 0x300 = ps_3_0
    unsigned long MaxTextureWidth;
    unsigned long MaxTextureHeight;
    unsigned long MaxVolumeExtent;
    unsigned char bSceneDepth;         // the depth buffer is an INTZ texture (GetSceneDepth works)
    unsigned char bFloat16Target;      // A16B16G16R16F render targets
    unsigned char bFloat16Blending;    // ... with alpha blending
    unsigned char bFloat32Target;      // R32F render targets
    unsigned char bVolumeTextures;
    unsigned char Reserved[3];
    // ---- version 2 ----
    // Instruction slots a vs_3_0 / ps_3_0 shader may use (D3DCAPS9 MaxVertexShader30-
    // InstructionSlots / MaxPixelShader30InstructionSlots). 512 is only the Shader Model 3
    // guaranteed minimum (D3DMIN30SHADERINSTRUCTIONS); drivers may report up to 32768
    // (D3DMAX30SHADERINSTRUCTIONS) -- an RTX 5080 reports 4096 for both. The ps_3_0 / vs_3_0
    // compiler enforces neither; the device refuses a shader over the driver's limit.
    unsigned long MaxVertexShaderSlots;
    unsigned long MaxPixelShaderSlots;
    // D3DCAPS9 MaxPShaderInstructionsExecuted: the executed-instruction cap per pixel (loops
    // count every iteration); 65535 or more on D3D10-class hardware.
    unsigned long MaxPixelShaderInstructionsExecuted;
    // D3DADAPTER_IDENTIFIER9::Description, truncated, NUL-terminated.
    char AdapterDescription[128];
};

// The size of the first published FRPExCaps (up to Reserved[3]); GetExCaps rejects less.
const unsigned long RPExCapsSizeV1 = 32;
static_assert(offsetof(FRPExCaps, MaxVertexShaderSlots) == RPExCapsSizeV1,
              "FRPExCaps version 1 layout changed");

// FRPExRenderTargetDesc: Width/Height 0 -> BackBufferScale * the back buffer size (at least 1
// texel), re-evaluated whenever the device is reset.
struct FRPExRenderTargetDesc
{
    unsigned long Width;
    unsigned long Height;
    float BackBufferScale;
    ERPExFormat Format;
};

enum ERPExPassFlags : unsigned long
{
    // The scene depth surface stays bound (depth test/write as the render states say).
    // Without it the pass runs with no depth surface and ZENABLE/ZWRITEENABLE off.
    RPExPassKeepDepth = 0x1,
    // Clear the colour target to ClearColor after binding it.
    RPExPassClearColor = 0x2,
};

struct FRPExPassDesc
{
    FRPExTexture* ColorTarget; // nullptr = the back buffer
    unsigned long Flags;       // ERPExPassFlags
    unsigned long ClearColor;  // A8R8G8B8, with RPExPassClearColor
};

// DirectX 9 resource / pipeline extensions. IID: Iids::RenderPipelineEx.
struct IRenderPipelineEx : IDAComponent
{
    // Device capabilities relevant to the extensions (Out->Size: see FRPExCaps). Fails outside
    // the device lifetime (GR_GENERIC) and for Out->Size < RPExCapsSizeV1 (GR_INVALID_PARMS).
    virtual GENRESULT DACOM_API GetExCaps(FRPExCaps* Out) = 0;

    // Compiles Path (FileSystem-relative, e.g. "SHADERS\\debug_depth.hlsl") as vs_3_0 VsEntry
    // and ps_3_0 PsEntry; #include is resolved through the same FileSystem (first next to the
    // file, then from the FileSystem root). A handle is returned even when compilation fails
    // (result GR_GENERIC, the compiler text is logged as an ERROR): the program is unusable
    // until ReloadExPrograms compiles it. FileSystem is AddRef'd and kept for reloads.
    virtual GENRESULT DACOM_API CreateExProgram(IFileSystem* FileSystem, const char* Path,
                                                const char* VsEntry, const char* PsEntry,
                                                FRPExProgram** Out) = 0;
    virtual GENRESULT DACOM_API DestroyExProgram(FRPExProgram* Program) = 0;
    // Re-reads and recompiles every program. A program that fails keeps its previous
    // bytecode and shaders (error logged). GR_OK only when all compiled.
    virtual GENRESULT DACOM_API ReloadExPrograms() = 0;

    virtual GENRESULT DACOM_API CreateExRenderTarget(const FRPExRenderTargetDesc* Desc,
                                                     FRPExTexture** Out) = 0;
    // A managed volume texture, one mip level. Data is Width*Height*Depth texels, rows and
    // slices tightly packed (L8: 1 byte, A8R8G8B8 / G16R16F / R32F: 4, R16F: 2, FP16 RGBA: 8).
    virtual GENRESULT DACOM_API CreateExVolumeTexture(unsigned long Width, unsigned long Height,
                                                      unsigned long Depth, ERPExFormat Format,
                                                      const void* Data, FRPExTexture** Out) = 0;
    // Render targets and volume textures only (the scene depth belongs to the pipeline).
    virtual GENRESULT DACOM_API DestroyExTexture(FRPExTexture* Texture) = 0;
    virtual GENRESULT DACOM_API GetExTextureSize(FRPExTexture* Texture, unsigned long* OutWidth,
                                                 unsigned long* OutHeight) = 0;
    // The scene depth buffer as a texture (hardware depth in .r, point sampling). Fails with
    // GR_GENERIC when the device has no INTZ support -- the depth buffer is then the ordinary
    // auto depth-stencil surface and cannot be sampled.
    virtual GENRESULT DACOM_API GetSceneDepth(FRPExTexture** Out) = 0;

    // Pass bracketing. Inside a pass: programs, Ex texture bindings, constants, Ex draws.
    // Passes do not nest.
    virtual GENRESULT DACOM_API BeginExPass(const FRPExPassDesc* Desc) = 0;
    virtual GENRESULT DACOM_API EndExPass() = 0;
    // Switches the colour target inside a pass (nullptr = back buffer); bKeepDepth binds the
    // scene depth surface, otherwise none. The viewport becomes the whole target.
    virtual GENRESULT DACOM_API SetExRenderTarget(FRPExTexture* ColorTarget,
                                                  unsigned long bKeepDepth) = 0;

    // nullptr returns to fixed function. Pass only; a program that never compiled fails.
    virtual GENRESULT DACOM_API BindExProgram(FRPExProgram* Program) = 0;
    virtual GENRESULT DACOM_API SetExVertexConstants(unsigned long StartRegister,
                                                     const float* Data,
                                                     unsigned long Vector4Count) = 0;
    virtual GENRESULT DACOM_API SetExPixelConstants(unsigned long StartRegister,
                                                    const float* Data,
                                                    unsigned long Vector4Count) = 0;
    // Sampler 0..15; Texture nullptr unbinds. Binding the scene depth while it is the bound
    // depth surface unbinds the depth surface for the rest of the pass.
    virtual GENRESULT DACOM_API BindExTexture(unsigned long Sampler, FRPExTexture* Texture,
                                              const FRPExSamplerState* State) = 0;
    // An IRenderPipeline / IRPTexture texture handle (CreateTexture, LoadTexture, LoadCubemap).
    virtual GENRESULT DACOM_API BindExPipelineTexture(unsigned long Sampler, void* Handle,
                                                      const FRPExSamplerState* State) = 0;
};

// DirectX 9 drawing extensions. IID: Iids::RPDrawEx. Pass only; a program must be bound.
struct IRPDrawEx : IDAComponent
{
    // Two triangles covering the viewport, FVF XYZ|TEX1: position in clip space (z 0) already
    // shifted by D3D9's half pixel for the current colour target, TEXCOORD0 (0,0) top-left ..
    // (1,1) bottom-right. The vertex shader passes the position through (see
    // DATA_EXTRA/SHADERS/common.hlsli, RPExFullscreenVS).
    virtual GENRESULT DACOM_API DrawFullscreenQuad() = 0;
    // DrawPrimitiveUP with the caller's FVF (D3DPRIMITIVETYPE / D3DFVF values are the same in
    // Direct3D 8 and 9). Count is the vertex count, as in IRenderPipeline::DrawPrimitive.
    virtual GENRESULT DACOM_API DrawExPrimitiveUp(unsigned long Prim, unsigned long Fvf,
                                                  const void* Verts, unsigned long Count) = 0;
};
