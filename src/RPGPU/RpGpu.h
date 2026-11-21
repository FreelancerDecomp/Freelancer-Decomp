// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU: the port-only SDL3 + SDL_GPU render pipeline (docs/re/rpgpu.md). It answers the same
// interfaces as RP8 (render/IRenderPipeline.h) and registers as the "SDLGPU" component, so the
// game selects it in DACOM.INI and talks to it through RP8's IIDs.
//
// This is the skeleton: it keeps every state the game reads back, holds textures and buffers
// in CPU memory, and on the GPU only clears and presents (backend/). The return values and
// output layouts follow RP8's, since the game checks them.

#include "dacom.h"
#include "render/IRenderPipeline.h"

#include "RpGpuCommon.h"
#include "RpGpuResources.h"
#include "RpGpuState.h"
#include "backend/RpGpuBackend.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

static constexpr char kRpGpuComponentName[] = "SDLGPU";

class FRpGpu;

class FRpGpuInner final : public IDAComponent
{
public:
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    unsigned long RefCount = 1;
    FRpGpu* Owner = nullptr;
};

class FRpGpuFactory final : public IComponentFactory
{
public:
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    unsigned long RefCount = 1;
};

class FRpGpu final : public IRenderPipeline,
                     public IVertexBufferManager,
                     public IRPDraw,
                     public IRPIndexBuffer,
                     public IRPVertexBuffer,
                     public IGammaControl,
                     public IRPTexture,
                     public IAggregateComponent
{
public:
    FRpGpu();
    ~FRpGpu();

    GENRESULT QueryLocal(const char* Iid, void** Out);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API Initialize() override;

    // IRenderPipeline
    GENRESULT DACOM_API Startup(const char* SectionName) override;
    GENRESULT DACOM_API ShutdownPipeline() override;
    GENRESULT DACOM_API SetPipelineState(int Id, int Value) override;
    GENRESULT DACOM_API GetPipelineState(int Id, int* Out) override;
    GENRESULT DACOM_API GetDeviceInfo(void* Out520) override;
    GENRESULT DACOM_API QueryDeviceAbility(unsigned int Index, unsigned long* Out) override;
    GENRESULT DACOM_API GetDisplayModeCount(unsigned int* Out) override;
    GENRESULT DACOM_API GetDisplayModeEnum(unsigned int Index, void* Out) override;
    GENRESULT DACOM_API SelectMode(int* Mode, unsigned int* OutIndex) override;
    GENRESULT DACOM_API CreateBuffers(HWND Hwnd, int* Mode, int* OutMode) override;
    GENRESULT DACOM_API GetModeBlob(void* OutAdapter, void* OutMode10Dwords) override;
    GENRESULT DACOM_API DestroyGpuResources() override;
    GENRESULT DACOM_API ClearBuffers(unsigned long Flags, const void* Rects) override;
    GENRESULT DACOM_API SwapBuffers() override;
    GENRESULT DACOM_API LockBuffer(void* Out) override;
    GENRESULT DACOM_API UnlockBuffer() override;
    GENRESULT DACOM_API GetBufferInterface(void* Out, void* Unused) override;
    GENRESULT DACOM_API GetDeviceStats(void* Out) override;
    GENRESULT DACOM_API SetViewportRect(int X, int Y, int W, int H) override;
    GENRESULT DACOM_API GetViewportRect(int* OutX, int* OutY, int* OutW, int* OutH) override;
    GENRESULT DACOM_API SetDepthRange(float MinZ, float MaxZ) override;
    GENRESULT DACOM_API GetDepthRange(float* OutMinZ, float* OutMaxZ) override;
    GENRESULT DACOM_API SetWindow(HWND Hwnd, int X, int Y, int W, int H) override;
    GENRESULT DACOM_API GetWindow(HWND* OutHwnd, int* OutLeft, int* OutRight, int* OutW, int* OutH) override;
    GENRESULT DACOM_API SetWorld(const float* M12) override;
    GENRESULT DACOM_API GetWorld(float* Out12) override;
    GENRESULT DACOM_API SetView(const float* M12) override;
    GENRESULT DACOM_API GetView(float* Out12) override;
    GENRESULT DACOM_API SetModelview(const float* M12) override;
    GENRESULT DACOM_API GetModelview(float* Out12) override;
    GENRESULT DACOM_API SetProjection(const float* M16) override;
    GENRESULT DACOM_API GetProjection(float* Out16) override;
    GENRESULT DACOM_API SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY, float AtZ, float UpX, float UpY, float UpZ) override;
    GENRESULT DACOM_API SetOrtho(float Left, float Right, float Bottom, float Top, float NearPlane, float FarPlane) override;
    GENRESULT DACOM_API SetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane) override;
    GENRESULT DACOM_API SetLight(void* Handle, const D3DLIGHT8* Light) override;
    GENRESULT DACOM_API DestroyLight(void* Handle) override;
    GENRESULT DACOM_API GetLight(void* Handle, D3DLIGHT8* OutLight) override;
    GENRESULT DACOM_API SetLightEnable(void* Handle, int Enable) override;
    GENRESULT DACOM_API GetLightEnable(void* Handle, int* OutEnable) override;
    GENRESULT DACOM_API SetMaterial(const D3DMATERIAL8* Material) override;
    GENRESULT DACOM_API GetMaterial(D3DMATERIAL8* Material) override;
    GENRESULT DACOM_API CreateTexture(int Width, int Height, unsigned int* Format, unsigned int Levels, unsigned int Flags, void** OutHandle) override;
    GENRESULT DACOM_API DestroyTexture(void* Handle) override;
    GENRESULT DACOM_API IsTexture(void* Handle) override;
    GENRESULT DACOM_API LockTexture(void* Handle, int Level, FRpTextureLock* Out) override;
    GENRESULT DACOM_API UnlockTexture(void* Handle, int Level) override;
    GENRESULT DACOM_API GetTextureFormat(void* Handle, int* OutFormat) override;
    GENRESULT DACOM_API GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH, int* OutLevels) override;
    GENRESULT DACOM_API GetTextureInterface(void* Handle, void* UnusedA, void* UnusedB) override;
    GENRESULT DACOM_API SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH, unsigned int Pitch, unsigned int* SrcFormat, const void* Pixels, void* A, void* B) override;
    GENRESULT DACOM_API BlitTexture(void* DstHandle, int DstLevel, void* SrcHandle, int SrcLevel) override;
    GENRESULT DACOM_API SetRenderTarget(void* Handle, void* UnusedA, void* UnusedB) override;
    GENRESULT DACOM_API GetRenderTarget(void* Handle) override;
    GENRESULT DACOM_API BeginScene() override;
    GENRESULT DACOM_API EndScene() override;
    GENRESULT DACOM_API ResetRenderStatesToDefaults() override;
    GENRESULT DACOM_API SetRenderState(D3DRENDERSTATETYPE State, int Value) override;
    GENRESULT DACOM_API GetRenderState(D3DRENDERSTATETYPE State, int* Out) override;
    GENRESULT DACOM_API SetTextureStageState(int Stage, int Type, int Value) override;
    GENRESULT DACOM_API GetTextureStageState(int Stage, int Type, int* Out) override;
    GENRESULT DACOM_API SetTextureStageTransform(int Stage, const float* Matrix16) override;
    GENRESULT DACOM_API GetTextureStageTransform(int Stage, float* OutMatrix16) override;
    GENRESULT DACOM_API SetTextureStageTexture(int Stage, void* Handle) override;
    GENRESULT DACOM_API GetTextureStageTexture(int Stage, void** OutHandle) override;
    GENRESULT DACOM_API VerifyState() override;
    GENRESULT DACOM_API DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts, unsigned int Count, void* Unused) override;
    GENRESULT DACOM_API DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf, const void* Verts, unsigned int VertCount, const void* Indices, unsigned int IndexCount, unsigned long Unused) override;
    GENRESULT DACOM_API DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex, unsigned int Count, void* Unused) override;
    GENRESULT DACOM_API DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb, unsigned int BaseVertex, unsigned int NumVerts, const void* Indices, unsigned int IndexCount, unsigned long Unused) override;
    GENRESULT DACOM_API AddLight(void* Handle) override;
    GENRESULT DACOM_API RemoveLight(void* Handle) override;
    GENRESULT DACOM_API UpdateLight(void* Handle) override;
    GENRESULT DACOM_API SetWorldN(int WorldIndex, const float* M12) override;
    // IVertexBufferManager
    GENRESULT DACOM_API VbMgrInit(void* A, void* B, void* C, void* D) override;
    GENRESULT DACOM_API VbMgrReleaseAll() override;
    GENRESULT DACOM_API LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out) override;
    GENRESULT DACOM_API UnlockVertexMemory(FVertexMemoryLock* Out) override;
    GENRESULT DACOM_API UnsupportedManagerOperation() override;
    GENRESULT DACOM_API CopyVertexData(void* Dest, unsigned int Fvf, const FRpVertexSrcDesc* Source, unsigned int StartVertex, unsigned int Count) override;
    // IRPDraw
    GENRESULT DACOM_API DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex, unsigned int NumVertices, unsigned int StartIndex, unsigned int Count) override;
    // IRPIndexBuffer
    GENRESULT DACOM_API CreateIb(unsigned int Count, unsigned int** Out, unsigned int Flags) override;
    GENRESULT DACOM_API DestroyIb(void** Handle) override;
    GENRESULT DACOM_API ResizeIb(void* Handle, unsigned int Count) override;
    GENRESULT DACOM_API CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData, unsigned int Count) override;
    GENRESULT DACOM_API LockIb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count) override;
    GENRESULT DACOM_API UnlockIb(void* Handle) override;
    GENRESULT DACOM_API SelectIb(void* Handle, unsigned int BaseVertexIndex, unsigned int Unused3, unsigned int Unused4) override;
    GENRESULT DACOM_API GetIbCapacity(void* Handle, unsigned int* OutCapacity) override;
    GENRESULT DACOM_API IsIbValid(void* Handle) override;
    // IRPVertexBuffer
    GENRESULT DACOM_API CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out, unsigned int Flags) override;
    GENRESULT DACOM_API DestroyVb(void** Handle) override;
    GENRESULT DACOM_API ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count) override;
    GENRESULT DACOM_API CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* SrcData, unsigned int StartVertex, unsigned int Count) override;
    GENRESULT DACOM_API LockVb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count) override;
    GENRESULT DACOM_API UnlockVb(void* Handle) override;
    GENRESULT DACOM_API UnsupportedVertexBufferOperation(void* Handle) override;
    GENRESULT DACOM_API SelectVb(void* Handle) override;
    GENRESULT DACOM_API GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount) override;
    GENRESULT DACOM_API IsVbValid(void* Handle) override;
    // IGammaControl
    GENRESULT DACOM_API SetGammaFunction(unsigned int ChannelMask, float Gamma, float Brightness, float Contrast, float Bias) override;
    GENRESULT DACOM_API SetGammaRamp(unsigned int ChannelMask, const unsigned short* RampData) override;
    GENRESULT DACOM_API GetGammaRamp(unsigned int Channel, unsigned short* OutRamp) override;
    GENRESULT DACOM_API SetGammaEnable(unsigned char Enable) override;
    GENRESULT DACOM_API GetGammaEnable() override;
    // IRPTexture
    GENRESULT DACOM_API PrintScreen(void* ParentFileSystem, const char* FileName) override;
    GENRESULT DACOM_API LoadTexture(IFileSystem* FileSystem, const char* FileName, void** OutHandle) override;
    GENRESULT DACOM_API LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize, const void* SrcData) override;
    GENRESULT DACOM_API GetImageInfo(_D3DXIMAGE_INFO* Info, unsigned int SrcSize, const void* SrcData) override;
    GENRESULT DACOM_API LoadCubemap(IFileSystem* FileSystem, const char* FileName, void** OutHandle) override;

    FRpGpuInner Inner_;
    IDAComponent* Outer_ = nullptr;

private:
    struct FMode
    {
        unsigned int Width;
        unsigned int Height;
        unsigned int Refresh;
        unsigned int PixelFormat; // RP8 pfenum: 3 R5G6B5, 8 X8R8G8B8
    };

    void ReadSettings();
    void ResetStateToDefaults();
    void ResetDeviceState();
    void LoadModes();
    static bool ResizeStorage(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int Stride);
    FRpGpuBuffer* PoolBuffer(unsigned int Fvf);
    FRpGpuBuffer* LiveVb(void* Handle) const;
    FRpGpuBuffer* LiveIb(void* Handle) const;
    FRpGpuTexture* LiveTexture(void* Handle) const;
    static bool LockRing(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int* OutStart);

    // The backend for the current setting (NULL_DEVICE), made on first use.
    IRpGpuBackend& Backend();

    std::unique_ptr<IRpGpuBackend> Backend_;
    bool bNullDevice_ = false;    // [RenderPipeline] NULL_DEVICE
    bool bBackendIsNull_ = false; // which kind Backend_ is

public:
    char Section_[64] = {}; // the ini section (CreateInstance's descriptor or Startup)

private:
    bool bStarted_ = false;
    bool bBuffersActive_ = false; // between a successful CreateBuffers and DestroyGpuResources
    bool bModesLoaded_ = false;
    std::vector<FMode> Modes_;

    int PipelineState_[14] = {};
    int RenderStates_[256] = {};
    int StageStates_[8][32] = {};
    // The defaults ResetRenderStatesToDefaults and CreateBuffers restore: RP8's, then the ini's.
    int DefaultRenderStates_[256] = {};
    int DefaultStageStates_[32] = {}; // every stage
    // The ability keys found in the ini section (RP8's ability map), by QueryDeviceAbility index.
    unsigned long AbilityOverrides_[0x15] = {};
    bool bAbilityOverridden_[0x15] = {};
    float Projection_[16] = {}; // D3D row-major
    int Viewport_[4] = {};
    float DepthRange_[2] = {0.0f, 1.0f};

    std::set<FRpGpuTexture*> Textures_;
    std::set<FRpGpuBuffer*> VertexBuffers_;
    std::set<FRpGpuBuffer*> IndexBuffers_;
    std::map<unsigned int, FRpGpuBuffer*> VbPools_; // FVF -> pool ring
    FRpGpuBuffer ScratchIb_;                        // handle -1
};
