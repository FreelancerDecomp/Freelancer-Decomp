// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// RP9 Ex: the port-only Direct3D 9 extensions of RP9.dll (render/IRenderPipelineEx.h). The
// interface methods are members of FDirectX9 (RP9.h) defined in RP9Ex.cpp; this header holds
// their state. Nothing here exists in RP8.dll or retail; see docs/re/rp9.md, "Ex extensions".

#include "RP9.h"

#include <string>
#include <vector>

// A shader program: the source location for reloads, the last bytecode that compiled, and the
// shader objects made from it (null while no device, or when the source never compiled).
struct FRPExProgram
{
    IFileSystem* FileSystem; // AddRef'd
    std::string Path;
    std::string VsEntry;
    std::string PsEntry;
    std::vector<unsigned char> VsBytecode;
    std::vector<unsigned char> PsBytecode;
    IDirect3DVertexShader9* Vs;
    IDirect3DPixelShader9* Ps;
};

enum ERp9ExTextureKind : unsigned int
{
    Rp9ExTextureRenderTarget = 0, // D3DPOOL_DEFAULT, re-created on every reset
    Rp9ExTextureVolume = 1,       // D3DPOOL_MANAGED, survives resets
    Rp9ExTextureSceneDepth = 2,   // the INTZ depth buffer (owned by FRp9ExState)
};

struct FRPExTexture
{
    ERp9ExTextureKind Kind;
    ERPExFormat Format;
    // Render targets: the creation request (Width/Height 0 = BackBufferScale of the back buffer).
    FRPExRenderTargetDesc TargetDesc;
    // The current size (render targets: after the last re-creation).
    unsigned long Width;
    unsigned long Height;
    unsigned long Depth;
    // IDirect3DTexture9 (render target, scene depth) or IDirect3DVolumeTexture9.
    IDirect3DBaseTexture9* Texture;
    // Level 0 of a render target / the scene depth surface; null for volumes.
    IDirect3DSurface9* Surface;
    // Volumes: the creation texels, for a device re-creation after ShutdownPipeline.
    std::vector<unsigned char> VolumeData;
};

struct FRp9ExState
{
    FRp9ExState();

    std::vector<FRPExProgram*> Programs;
    // Render targets and volume textures created through the interface.
    std::vector<FRPExTexture*> Textures;
    // The INTZ depth buffer; Texture/Surface null when the device has no INTZ depth.
    FRPExTexture SceneDepth;
    // A plain depth surface made when the INTZ texture could not be created after the device
    // was already made without an auto depth-stencil.
    IDirect3DSurface9* FallbackDepth;
    unsigned long BackBufferWidth;
    unsigned long BackBufferHeight;

    // The open pass (BeginExPass .. EndExPass).
    bool bInPass;
    bool bDepthBound;
    IDirect3DSurface9* SavedColor;
    IDirect3DSurface9* SavedDepth;
    D3DVIEWPORT9 SavedViewport;
    unsigned char bSavedViewportRectValid;
    unsigned char bSavedDepthRangeValid;
    unsigned long SavedPending[FRp9RenderStateCache::StateCount];
    // Bit per sampler 0..15 the pass bound a texture on; stages 0..7 also saved their cache.
    unsigned long TouchedSamplers;
    FRp9ExStageSave SavedStages[FRp9DeviceStateCaches::StageCount];
    // Bit per sampler that holds the scene depth.
    unsigned long SceneDepthSamplers;
    // What the pass bound on each sampler (not AddRef'd; Get* is unavailable on a pure device).
    IDirect3DBaseTexture9* BoundTextures[16];
    FRPExProgram* BoundProgram;
    // The pass's colour target (null = the back buffer) and its size.
    FRPExTexture* CurrentTarget;
    unsigned long TargetWidth;
    unsigned long TargetHeight;
};
