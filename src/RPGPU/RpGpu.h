// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU: the port-only SDL3 + SDL_GPU render pipeline (docs/re/rpgpu.md). It answers the same
// interfaces as RP8 (render/IRenderPipeline.h) and registers as the "SDLGPU" component, so the
// game selects it in DACOM.INI and talks to it through RP8's IIDs.
//
// It keeps every state the game reads back, holds textures and buffers in CPU memory, and
// records each frame's draws for the backend (backend/) to replay. The return values and
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
    GENRESULT DACOM_API CreateBuffers(RpWindow Hwnd, int* Mode, int* OutMode) override;
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
    GENRESULT DACOM_API SetWindow(RpWindow Hwnd, int X, int Y, int W, int H) override;
    GENRESULT DACOM_API GetWindow(RpWindow* OutHwnd, int* OutLeft, int* OutRight, int* OutW, int* OutH) override;
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
    GENRESULT DACOM_API SetLight(void* Handle, const RpLight* Light) override;
    GENRESULT DACOM_API DestroyLight(void* Handle) override;
    GENRESULT DACOM_API GetLight(void* Handle, RpLight* OutLight) override;
    GENRESULT DACOM_API SetLightEnable(void* Handle, int Enable) override;
    GENRESULT DACOM_API GetLightEnable(void* Handle, int* OutEnable) override;
    GENRESULT DACOM_API SetMaterial(const RpMaterial* Material) override;
    GENRESULT DACOM_API GetMaterial(RpMaterial* Material) override;
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
    GENRESULT DACOM_API SetRenderState(RpRenderState State, int Value) override;
    GENRESULT DACOM_API GetRenderState(RpRenderState State, int* Out) override;
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
    GENRESULT DACOM_API GetImageInfo(RpImageInfo* Info, unsigned int SrcSize, const void* SrcData) override;
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
    FRpGpuImage* LockableImage(FRpGpuTexture* Texture, int Level) const;
    GENRESULT MakeCubemap(const void* Data, unsigned long Size, void** OutHandle);
    static bool LockRing(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int* OutStart);

    // The backend for the current setting (NULL_DEVICE), made on first use.
    IRpGpuBackend& Backend();

    // RpGpuDraw.cpp: records one draw into Frame_, and uploads a texture that changed. Verts is
    // the first vertex the draw (or its indices) may use and VertCount how many follow it;
    // Stride is bytes per vertex; Indices is null for a draw without indices.
    void RecordDraw(unsigned long Prim, unsigned int Fvf, const unsigned char* Verts, unsigned int Stride,
                    unsigned int VertCount, const std::uint16_t* Indices, unsigned int IndexCount);
    void RefreshTexture(FRpGpuTexture* Texture);

    // RpGpuState.cpp: RP8's transform stores (FRp8TransformCache). The world and view matrices
    // are kept as Direct3D's 4x4 (row-major, row vectors), as RP8 sends them to the device.
    void StoreViewIdentity(bool bFlip);
    void ApplyIdentityTransforms(); // CreateBuffers: RP8's FUN_06d03c94
    void FlushDrawState();        // every draw: RP8's FlushDrawState (the view, the lights)
    void ResetStreams();            // RP8's FRp8StreamCache::Reset
    void ResetTextureStages();      // RP8's ApplyDefaultTextureStages, for the stage bindings

    // RpGpuLighting.cpp: RP8's light manager (FRp8LightManager) and the uniform blocks a draw's
    // lighting and fog need.
    struct FLightRecord
    {
        bool bEnabled = false;       // the enable state last committed to the "device"
        bool bWantEnabled = false;   // what SetLightEnable asked for
        bool bDataDirty = false;     // Data is resent at the next flush
        std::uint32_t Generation = 0xFFFFFFFFu; // the flush generation it was queued in
        int Index = -1;              // the light index from the pool
        const RpLight* Data = nullptr; // AddLight: the caller's own light, read at each flush
    };
    // Direct3D's light table: what SetLight and LightEnable left at each index.
    struct FDeviceLight
    {
        RpLight Data = {};
        bool bSet = false;
        bool bEnabled = false;
    };
    FLightRecord* FindLight(void* Handle);
    int FindOrCreateLight(void* Handle, const RpLight* Data);
    void EraseLight(FLightRecord* Record, void* Handle);
    void QueueLight(FLightRecord* Record);
    void SetLightWanted(void* Handle, int Enable);
    void FlushLights();
    void ResetLights();             // a new device: every light is sent again
    FDeviceLight& DeviceLight(int Index);
    bool DeviceLive(); // RP8's NeedDeviceLifetime
    void FillLightingAndFog(FRpGpuCommand* Command, bool bPretransformed, unsigned int FvfColours);

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
    float World_[16] = {}; // world matrix 0 (SetWorld, or SetWorldN(0))
    // SetWorldN's world matrices 1..255 for vertex blending, as Direct3D holds them; [0] unused.
    float WorldN_[kRpGpuWorldMatrices][16] = {};
    // SetWorldN(0) replaces the matrix drawn with, as Direct3D's WORLDMATRIX(0) is D3DTS_WORLD,
    // but RP8's transform cache, which GetWorld reads, keeps SetWorld's: WorldCache_, while
    // bWorldOverridden_. The next store into the world ends it.
    float WorldCache_[16] = {};
    bool bWorldOverridden_ = false;
    float View_[16] = {};
    // RP8's view store flags: bViewCommitted_ is cleared by every store and set by the next
    // draw; bViewMarker_ is set when SetModelview or SetLookat put in their identity view, and
    // cleared when a draw commits a changed view. While it is set they leave the view alone.
    bool bViewCommitted_ = false;
    bool bViewMarker_ = false;
    // RP8's stream selection (FRp8StreamCache): SelectVb, SelectIb and the draws that set them.
    // SelectedIb_ is an index buffer or kMinusOne (the scratch ring); IbBaseVertex_ is
    // SelectIb's base vertex. 0xFFFFFFFF after a reset, as in RP8.
    void* SelectedVb_ = nullptr;
    void* SelectedIb_ = nullptr;
    unsigned int IbBaseVertex_ = 0xFFFFFFFFu;
    // RP8's light manager: records by handle, the queue for the next flush, the index pool.
    std::map<void*, FLightRecord> LightRecords_;
    std::vector<FLightRecord*> DirtyLights_; // null where a queued record was removed
    // RP8 counts flushes in a signed long and every draw flushes; unsigned here, so the count
    // wraps where RP8's would overflow.
    std::uint32_t LightGeneration_ = 0;
    std::vector<int> LightIndexPool_ = std::vector<int>(0x40, 0);
    unsigned int LightIndicesUsed_ = 0;
    unsigned int LightIndexNext_ = 0;
    std::vector<FDeviceLight> DeviceLights_;
    RpMaterial Material_ = {}; // SetMaterial's, as Direct3D keeps it
    int Viewport_[4] = {};
    float DepthRange_[2] = {0.0f, 1.0f};
    void* StageTextures_[8] = {}; // SetTextureStageTexture's handles
    // SetTextureStageTransform's matrices, as Direct3D holds them (RP8 stores the caller's
    // transposed).
    float StageTransforms_[8][16] = {};
    FRpGpuFrameRecording Frame_;  // the draws since the last SwapBuffers
    std::uint64_t NextTextureVersion_ = 1;
    // Image versions replaced or destroyed during this frame: the frame's draws may still use
    // them, so the backend lets go of them only after SwapBuffers has presented it.
    std::vector<std::uint64_t> RetiredVersions_;

    std::set<FRpGpuTexture*> Textures_;
    std::set<FRpGpuBuffer*> VertexBuffers_;
    std::set<FRpGpuBuffer*> IndexBuffers_;
    std::map<unsigned int, FRpGpuBuffer*> VbPools_; // FVF -> pool ring
    FRpGpuBuffer ScratchIb_;                        // handle -1
};
