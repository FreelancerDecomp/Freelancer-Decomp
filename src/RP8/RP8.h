// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "luaprofile.h"
#include "render/IRenderPipeline.h"
#include "render/d3d8_min.h"

#include <stddef.h>

#include <map>
#include <vector>

static const unsigned long kRp8ObjectSize = 0x230C;
static const unsigned long kRp8RegisterPriority = 0x40000000;
static const unsigned int kAbilityCount = 21;

struct FRp8AggDesc
{
    unsigned long Size;
    const char* Name;
    IDAComponent* Outer;
    IDAComponent** InnerOut;
    const char* Extra;
};

struct FRp8Mode
{
    int Field00;
    int Field04;
    int Width;
    int Height;
    int Format;
    int WantedZ;
    int WantedStencil;
    int Interval;
    int BufferCount;
    unsigned char SwapExtra;
    unsigned char Fullscreen;
    unsigned char Pad26[2];
};

static_assert(sizeof(FRp8Mode) == 0x28, "mode 10 dword");

// Profile property metadata and the value read at startup.
struct FRp8Property
{
    const char* Name;
    int Id;
    unsigned long Default;
    unsigned long Value;
    unsigned char bEnabled;
};

typedef std::map<int, FRp8Property> FRp8PropertyMap;

// Places a container at the start of a Size-byte slot of the retail object layout: retail's
// VC6 map is 0x14 bytes and its vector 0x10; the current STL ones are no bigger, the rest of
// the slot is padding (an empty base when nothing is left over).
template <unsigned int PadBytes>
struct TRp8SlotPad
{
    unsigned char Pad[PadBytes];
};

template <>
struct TRp8SlotPad<0>
{
};

template <typename T, unsigned int Size>
struct TRp8Slot : TRp8SlotPad<(unsigned int)(Size - sizeof(T))>
{
    T Value;
};

// FUN_06d15bf0: one display-mode record of an adapter (0x1C bytes). The constructor only sets
// +0x10 and +0x14 (FUN_06d5cc30(0), the pf index of format 0); nothing in rp8.dll fills the rest.
struct FRp8AdapterMode
{
    unsigned long Field00;
    unsigned long Field04;
    unsigned long Field08;
    unsigned long Field0C;
    unsigned long PfIndex10;
    unsigned long PfIndex14;
    unsigned long Field18;
};

static_assert(sizeof(FRp8AdapterMode) == 0x1C, "adapter mode record 0x1C");

// One element (0x520 bytes) of the adapter vector at object+0x198, filled by startup
// (06d0691b..06d06ac5) and copied out whole by get_device_info.
struct FRp8Adapter
{
    D3DADAPTER_IDENTIFIER8 Identifier; // +0x000 GetAdapterIdentifier(adapter, 2, ..)
    D3DDISPLAYMODE DisplayMode;        // +0x42C GetAdapterDisplayMode
    D3DCAPS8 Caps;                     // +0x43C GetDeviceCaps(adapter, object+0x1AC, ..)
    unsigned long Field510;            // +0x510 TODO: no writer found in startup
    unsigned long CanRenderWindowed;   // +0x514 Caps.Caps2 & D3DCAPS2_CANRENDERWINDOWED
    FRp8AdapterMode* Modes;            // +0x518 operator new(ModeCount * 0x1C)
    unsigned long ModeCount;           // +0x51C GetAdapterModeCount
};

static_assert(sizeof(FRp8Adapter) == 0x520, "adapter record 0x520");

// A vertex-buffer node (0x24 bytes): create_vb operator new(0x24) for list buffers, the value
// of the VB pool map for pooled ones (FUN_06d1b480(0x200) builds the pooled value: Flags =
// 0x200|8, the rest 0 except +0x1C).
struct FRp8Vb
{
    void ReleaseGpu(); // formerly ReleaseVbGpu

    unsigned long Flags;
    unsigned long Fvf;
    unsigned long Count;
    unsigned long Used;
    unsigned long Locked;
    FRp8Vb* Next;
    FRp8Vb* Prev;
    unsigned long Field1C;
    IDirect3DVertexBuffer8* Gpu;
};

static_assert(sizeof(FRp8Vb) == 0x24, "create_vb operator new(0x24)");

struct FRp8Ib
{
    HRESULT LockRange(unsigned int Start, unsigned int Count, void** OutBits,
                      unsigned int* OutStart, unsigned char bSysLock); // formerly LockIbRange

    unsigned long Flags;
    IDirect3DIndexBuffer8* Gpu;
    void* LockedBits;
    FRp8Ib* Next;
    FRp8Ib* Prev;
    unsigned long Cursor;
    unsigned long Capacity;
};

static_assert(sizeof(FRp8Ib) == 0x1C, "index buffer node");

enum ERp8StreamDirtyFlag : unsigned char
{
    StreamDirtyVertexSource = 1,
    StreamDirtyIndices = 2
};

struct FRp8StreamDirtyFlags
{
    unsigned char Dirty;
    unsigned char Reserved[3];
};

struct FRp8StreamCache
{
    void Reset(); // formerly ResetStreamCache
    void SelectVertexStream(FRp8Vb* Buffer, unsigned long Fvf); // formerly MarkStreamVb / MarkStreamFvf
    void SelectIndices(FRp8Ib* Buffer, unsigned int BaseVertex); // formerly MarkStreamScratchIb / SelectIb cache stores

    unsigned long Fvf;
    FRp8Vb* VertexBuffer;
    FRp8Ib* IndexBuffer;
    unsigned int BaseVertex;
    FRp8StreamDirtyFlags Flags; // selection touches Dirty; reset clears all four bytes
};

static_assert(sizeof(FRp8StreamDirtyFlags) == 4, "stream flag word");
static_assert(offsetof(FRp8StreamCache, VertexBuffer) == 4, "stream vertex buffer");
static_assert(offsetof(FRp8StreamCache, IndexBuffer) == 8, "stream index buffer");
static_assert(offsetof(FRp8StreamCache, BaseVertex) == 0xC, "stream base vertex");
static_assert(offsetof(FRp8StreamCache, Flags) == 0x10, "stream dirty flags");
static_assert(sizeof(FRp8StreamCache) == 0x14, "stream cache");

struct FRp8TextureStageState
{
    unsigned long Value;
    unsigned char bValid;
    unsigned char Reserved[3];
};

struct FRp8TextureBinding
{
    void* Handle;
    unsigned char bValid;
    unsigned char Reserved[3];
};

struct FRp8TransformCache
{
    void Commit(IDirect3DDevice8* Device, D3DTRANSFORMSTATETYPE Type); // formerly Rp8CommitTransform
    void StoreAffine(const float* Matrix); // formerly StoreMatrix12
    void LoadAffine(float* Matrix) const; // formerly LoadMatrix12
    void StoreTransposed(const float* Matrix); // formerly StoreMatrix16
    void LoadTransposed(float* Matrix) const; // formerly LoadMatrix16

    float Values[16];
    unsigned char bCommitted;
    unsigned char bApplyMarker; // combined apply sets this; device submission clears it
    unsigned char Reserved[2];
};

struct FRp8DeviceStateCaches
{
    static const unsigned int StageCount = 8;
    static const unsigned int StateCount = 29;

    unsigned long ReadTextureStageState(IDirect3DDevice8* Device, unsigned long Stage,
                                       D3DTEXTURESTAGESTATETYPE Type); // formerly ReadTssCache

    FRp8TextureStageState TextureStates[StageCount][StateCount]; // stage stride 0xE8
    FRp8TextureBinding TextureBindings[StageCount];
    FRp8TransformCache TextureTransforms[StageCount];
};

struct FRp8PipelineTransforms
{
    FRp8TransformCache View;
    FRp8TransformCache World;
    FRp8TransformCache Projection;
};

static_assert(sizeof(FRp8TextureStageState) == 8, "texture-stage state");
static_assert(sizeof(FRp8TextureBinding) == 8, "texture binding");
static_assert(sizeof(FRp8TransformCache) == 0x44, "transform cache");
static_assert(offsetof(FRp8TransformCache, bCommitted) == 0x40, "transform submission flag");
static_assert(offsetof(FRp8TransformCache, bApplyMarker) == 0x41, "transform apply marker");
static_assert(sizeof(((FRp8DeviceStateCaches*)0)->TextureStates[0]) == 0xE8, "texture-stage row stride");
static_assert(offsetof(FRp8DeviceStateCaches, TextureBindings) == 0x740, "texture bindings");
static_assert(offsetof(FRp8DeviceStateCaches, TextureTransforms) == 0x780, "texture transforms");
static_assert(sizeof(FRp8DeviceStateCaches) == 0x9A0, "device-state caches");
static_assert(offsetof(FRp8PipelineTransforms, World) == 0x44, "world transform");
static_assert(offsetof(FRp8PipelineTransforms, Projection) == 0x88, "projection transform");
static_assert(sizeof(FRp8PipelineTransforms) == 0xCC, "pipeline transforms");

// A format class owns an ordered array of candidate format IDs.
struct FormatClassNode
{
    unsigned int Key;
    unsigned int Count;
    unsigned int* Values;
};

static_assert(sizeof(FormatClassNode) == 0xC, "format class node 3 dword");

// The StateCache.cpp light manager embedded at pipeline +0x21F4 (constructor FUN_06d2c31c
// writes +4 then +0). The light records themselves live in StateCache globals (RP8.cpp).
struct FRp8LightManager
{
    void Initialize(); // formerly LightManagerConstruct
    int FindOrCreate(unsigned long Handle, const D3DLIGHT8* Data); // formerly LightFindOrCreate
    bool Remove(unsigned long Handle); // formerly LightRemove
    unsigned char GetEnable(unsigned long Handle); // formerly LightGetEnable
    bool Update(unsigned long Handle); // formerly LightUpdate
    bool SetEnable(unsigned long Handle, int Enable); // formerly LightSetEnable
    void Reset(); // formerly LightReset
    HRESULT Flush(IDirect3DDevice8* Device); // formerly LightFlush

    long EnabledCount; // +0 lights whose LightEnable state on the device is on
    long Generation;   // +4 flush counter (FUN_06d2d032 increments, FUN_06d2ce6a zeroes)
};

static_assert(sizeof(FRp8LightManager) == 8, "light manager 2 dword");

class FDirectX8;
class FMgsdbDatabase;

struct FRp8RenderStateEntry
{
    unsigned long Cached;
    unsigned long Pending;
    unsigned long Generation;
    unsigned long bEnabled;
};

struct FRp8RenderStateCache
{
    static const unsigned int StateCount = 256;

    void Initialize(); // formerly InitRsQueue
    void ResetGeneration(); // formerly RsQueueResetGeneration
    unsigned long GetPending(unsigned int StateId) const; // formerly RsQueueReadPending
    HRESULT Apply(IDirect3DDevice8* Device, unsigned int StateId, unsigned long Value); // formerly RsQueueApply
    bool MarkDirty(unsigned int StateId, unsigned long Value); // formerly RsQueueMarkDirty
    void Flush(IDirect3DDevice8* Device); // formerly RsQueueFlush

    FRp8RenderStateEntry Entries[StateCount];
    unsigned long DirtyStates[StateCount];
    unsigned long DirtyCount;
    unsigned long Generation;
};

static_assert(sizeof(FRp8RenderStateEntry) == 0x10, "render-state entry");
static_assert(offsetof(FRp8RenderStateCache, DirtyStates) == 0x1000, "dirty states");
static_assert(offsetof(FRp8RenderStateCache, DirtyCount) == 0x1400, "dirty count");
static_assert(offsetof(FRp8RenderStateCache, Generation) == 0x1404, "generation");

class FRp8Inner final : public IDAComponent
{
public:
    FRp8Inner();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    unsigned long RefCount;
    FDirectX8* Owner;
};

static_assert(offsetof(FRp8Inner, RefCount) == 0x4, "inner reference count");
static_assert(offsetof(FRp8Inner, Owner) == 0x8, "inner owner");
static_assert(sizeof(FRp8Inner) == 0xC, "inner controller");

class FRp8Factory final : public IComponentFactory
{
public:
    FRp8Factory();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    unsigned long RefCount;
    const char* Name;
};

class FDirectX8 final : public IRenderPipeline,
                        public IVertexBufferManager,
                        public IRPDraw,
                        public IRPIndexBuffer,
                        public IRPVertexBuffer,
                        public IGammaControl,
                        public IRPTexture,
                        public IAggregateComponent
{
public:
    FDirectX8();
    ~FDirectX8();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

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
    GENRESULT DACOM_API GetWindow(HWND* OutHwnd, int* OutLeft, int* OutRight, int* OutW,
                                  int* OutH) override;
    GENRESULT DACOM_API SetWorld(const float* M12) override;
    GENRESULT DACOM_API GetWorld(float* Out12) override;
    GENRESULT DACOM_API SetView(const float* M12) override;
    GENRESULT DACOM_API GetView(float* Out12) override;
    GENRESULT DACOM_API SetModelview(const float* M12) override;
    GENRESULT DACOM_API GetModelview(float* Out12) override;
    GENRESULT DACOM_API SetProjection(const float* M16) override;
    GENRESULT DACOM_API GetProjection(float* Out16) override;
    GENRESULT DACOM_API SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                  float AtZ, float UpX, float UpY, float UpZ) override;
    GENRESULT DACOM_API SetOrtho(float Left, float Right, float Bottom, float Top,
                                 float NearPlane, float FarPlane) override;
    GENRESULT DACOM_API SetPerspective(float FovDeg, float Aspect, float NearPlane,
                                       float FarPlane) override;
    GENRESULT DACOM_API SetLight(void* Handle, const D3DLIGHT8* Light) override;
    GENRESULT DACOM_API DestroyLight(void* Handle) override;
    GENRESULT DACOM_API GetLight(void* Handle, D3DLIGHT8* OutLight) override;
    GENRESULT DACOM_API SetLightEnable(void* Handle, int Enable) override;
    GENRESULT DACOM_API GetLightEnable(void* Handle, int* OutEnable) override;
    GENRESULT DACOM_API SetMaterial(const D3DMATERIAL8* Material) override;
    GENRESULT DACOM_API GetMaterial(D3DMATERIAL8* Material) override;
    GENRESULT DACOM_API CreateTexture(int Width, int Height, unsigned int* Format,
                                      unsigned int Levels, unsigned int Flags,
                                      void** OutHandle) override;
    GENRESULT DACOM_API DestroyTexture(void* Handle) override;
    GENRESULT DACOM_API IsTexture(void* Handle) override;
    GENRESULT DACOM_API LockTexture(void* Handle, int Level, void* Out) override;
    GENRESULT DACOM_API UnlockTexture(void* Handle, int Level) override;
    GENRESULT DACOM_API GetTextureFormat(void* Handle, int* OutFormat) override;
    GENRESULT DACOM_API GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH,
                                      int* OutLevels) override;
    GENRESULT DACOM_API GetTextureInterface(void* Handle, void* UnusedA, void* UnusedB) override;
    GENRESULT DACOM_API SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH,
                                            unsigned int Pitch, unsigned int* SrcFormat,
                                            const void* Pixels, void* A, void* B) override;
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
    GENRESULT DACOM_API DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                      unsigned int Count, void* Unused) override;
    GENRESULT DACOM_API DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                               unsigned int VertCount, const void* Indices,
                                               unsigned int IndexCount, unsigned long Unused) override;
    GENRESULT DACOM_API DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex,
                                        unsigned int Count, void* Unused) override;
    GENRESULT DACOM_API DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb, unsigned int BaseVertex,
                                               unsigned int NumVerts, const void* Indices,
                                               unsigned int IndexCount, unsigned long Unused) override;
    GENRESULT DACOM_API AddLight(void* Handle) override;
    GENRESULT DACOM_API RemoveLight(void* Handle) override;
    GENRESULT DACOM_API UpdateLight(void* Handle) override;
    GENRESULT DACOM_API SetWorldN(int WorldIndex, const float* M12) override;

    GENRESULT DACOM_API VbMgrInit(void* A, void* B, void* C, void* D) override;
    GENRESULT DACOM_API VbMgrReleaseAll() override;
    GENRESULT DACOM_API LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out) override;
    GENRESULT DACOM_API UnlockVertexMemory(FVertexMemoryLock* Out) override;
    GENRESULT DACOM_API UnsupportedManagerOperation() override; // formerly VbMgrSlot7
    GENRESULT DACOM_API CopyVertexData(void* Dest, unsigned int Fvf,
                                      const FRpVertexSrcDesc* Source,
                                      unsigned int StartVertex, unsigned int Count) override;

    GENRESULT DACOM_API DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex,
                                             unsigned int NumVertices, unsigned int StartIndex,
                                             unsigned int Count) override;

    GENRESULT DACOM_API CreateIb(unsigned int Count, unsigned int** Out,
                                 unsigned int Flags) override;
    GENRESULT DACOM_API DestroyIb(void** Handle) override;
    GENRESULT DACOM_API ResizeIb(void* Handle, unsigned int Count) override;
    GENRESULT DACOM_API CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData, unsigned int Count) override;
    GENRESULT DACOM_API LockIb(void* Handle, unsigned int* Offset, void** Out,
                               unsigned int Count) override;
    GENRESULT DACOM_API UnlockIb(void* A) override;
    GENRESULT DACOM_API SelectIb(void* A, unsigned int BaseVertexIndex,
                                 unsigned int Unused3, unsigned int Unused4) override;
    GENRESULT DACOM_API GetIbCapacity(void* A, unsigned int* OutCapacity) override;
    GENRESULT DACOM_API IsIbValid(void* A) override;

    GENRESULT DACOM_API CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out,
                                 unsigned int Flags) override;
    GENRESULT DACOM_API DestroyVb(void** A) override;
    GENRESULT DACOM_API ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count) override;
    GENRESULT DACOM_API CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* SrcData, unsigned int StartVertex, unsigned int Count) override;
    GENRESULT DACOM_API LockVb(void* Handle, unsigned int* Offset, void** Out,
                               unsigned int Count) override;
    GENRESULT DACOM_API UnlockVb(void* A) override;
    GENRESULT DACOM_API UnsupportedVertexBufferOperation(void* Handle) override; // formerly VbSlot9
    GENRESULT DACOM_API SelectVb(void* A) override;
    GENRESULT DACOM_API GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount) override;
    GENRESULT DACOM_API IsVbValid(void* A) override;

    GENRESULT DACOM_API SetGammaEnable(unsigned char Enable) override;
    GENRESULT DACOM_API GetGammaEnable() override;
    GENRESULT DACOM_API SetGammaFunction(unsigned int ChannelMask, float Gamma, float Brightness,
                                         float Contrast, float Bias) override;
    GENRESULT DACOM_API SetGammaRamp(unsigned int ChannelMask, const unsigned short* RampData) override;
    GENRESULT DACOM_API GetGammaRamp(unsigned int Channel, unsigned short* OutRamp) override;

    GENRESULT DACOM_API PrintScreen(void* ParentFileSystem, const char* FileName) override;
    GENRESULT DACOM_API LoadTexture(IFileSystem* FileSystem, const char* FileName,
                                    void** OutHandle) override;
    GENRESULT DACOM_API LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize,
                                        const void* SrcData) override;
    GENRESULT DACOM_API GetImageInfo(_D3DXIMAGE_INFO* Info, unsigned int SrcSize,
                                     const void* SrcData) override;
    GENRESULT DACOM_API LoadCubemap(IFileSystem* FileSystem, const char* FileName,
                                    void** OutHandle) override;

    GENRESULT DACOM_API Initialize() override;

    GENRESULT QueryLocal(const char* Iid, void** Out);
    void FlushDrawState(IDirect3DDevice8* Device); // formerly static FlushDrawState
    GENRESULT ApplySectionName(const FRp8AggDesc* Desc);
    // FUN_06d020ab (startup and create_buffers call it with the device caps). The flag part
    // runs only after the MGSDB database loaded; the ability-map overrides run on every path.
    void ApplyDeviceCaps(const D3DCAPS8* Caps);
    void ApplyMgsdbFlags(FMgsdbDatabase* Db);
    void ApplyAbilityOverrides();
    unsigned long QueryVidMem();
    D3DFORMAT PickDepthFormat(D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, int* WantedZ,
                              int* WantedS);
    bool CheckTextureFormat(unsigned int Format, unsigned long Usage, D3DRESOURCETYPE RType);
    unsigned int PrimCountFromVerts(unsigned long Prim, unsigned int Count);
    void EnumAdapters();
    // Shared lifetime checks retain the diagnostic and source line of each caller.
    GENRESULT NeedStartup(const char* Message, int Line);
    GENRESULT NeedBuffers(const char* Message, int Line);
    GENRESULT NeedDeviceLifetime(const char* Message, int Line);
    unsigned char* ObjectBase();
    // FUN_06d0b607 / FUN_06d0bb58: MulTransform(diag(1,1,-1), M12) — negate row2 + Tz.
    void MulZFlip12(float* Out12, const float* In12);
    void ApplyIdentityTransforms();
    void ApplyDeviceViewport();
    void ApplyDefaultRenderStates();
    void ApplyDefaultTextureStages();
    void ApplyPostDeviceHelpers();
    void UnbindTextureHandle(void* Handle);
    unsigned int PfIndex(unsigned int Format);
    unsigned int PfToD3d(unsigned int Format);
    unsigned int PfBytesPerPixel(unsigned int Format);
    unsigned int FvfStride(unsigned int Fvf);
    FRp8Vb* FindOrInsertPoolVb(unsigned int Fvf);
    void GrowPoolVb(FRp8Vb* Wrapper, unsigned int Fvf, unsigned int Count);
    void ReleasePoolVbs();
    unsigned long StageCount();
    int PipelineStateValue(int Id);

    // Pixel-format-class catalog (retail FUN_06d04693/FUN_06d046e6/FUN_06d047df).
    FormatClassNode* FindFormatClass(unsigned int Key);
    FormatClassNode* FindOrInsertFormatClass(unsigned int Key);
    void ClearFormatCatalog();
    void ClearFormatClassCandidates(FormatClassNode* Node);
    void AppendFormatCandidate(FormatClassNode* Node, unsigned int Value);
    unsigned int ResolvePixelFormatByComponents(unsigned int Bpp, unsigned int RBits,
                                                unsigned int GBits, unsigned int BBits,
                                                unsigned int AlphaBits);
    unsigned int FormatClassKeyFromPfId(unsigned int PfId);
    void PopulateFormatCatalog();

    IProfileParser* Parser_;
    // FUN_06d01a06 strcpy's AGGDESC+0x10 (or "RenderPipeline") here; startup(0) reads this.
    char SectionName_[0x80];
    // The section startup last read DeviceId/DeviceType/DeviceTypeFallback from (strcpy at
    // 06d06552); a startup with the same name skips that block. The ability map is read from
    // this section too (06d07857).
    char LastSection_[0x80];
    unsigned long CreateFlags_;
    unsigned long SwapFlags_;
    IDirect3D8* D3d_;
    IDirect3DDevice8* Device_;
    IDirect3DSurface8* LockBufferSurf_;
    FRp8StreamCache StreamCache_;
    // +0x14C: first dword (usage flags) of the inline scratch IB node; the constructor writes
    // 0x200|8 (06d01220).
    unsigned long ScratchIbFlags_;
    IDirect3DIndexBuffer8* ScratchIb_;
    unsigned char Pad154_[0x160 - 0x154];
    unsigned long IbUsed_;
    unsigned long IbCapacity_;
    unsigned long VidMem_;
    // +0x16C: the VBMgr / create_vb(flags & 1) pool, retail map<FVF, vertex-buffer node>
    // (constructed by FUN_06d1d490, find-or-insert FUN_06d19aa0, node value at node+0x10,
    // erased with FUN_06d1bc60 per value by FUN_06d1a780 in the destructor).
    TRp8Slot<std::map<unsigned long, FRp8Vb>, 0x14> VbPool_;
    // +0x180: bytes held by the pool's vertex buffers (stride * count per entry).
    unsigned long VbPoolBytes_;
    // bit0: create_buffers .. destroy_buffers window (the "called outside of
    // create_buffers/destroy_buffers" checks). bit1: IGammaControl enable (set_gamma_enable
    // FUN_06d1535d writes it, set_gamma_function / set_gamma_ramp pass it as D3DSGR_CALIBRATE).
    unsigned long DeviceReady_;
    // Opaque catalog header; candidates are owned by the begin/end/capacity range.
    unsigned char FormatCatalogHeader_[4];
    FormatClassNode* FormatCatalogBegin_;
    FormatClassNode* FormatCatalogEnd_;
    FormatClassNode* FormatCatalogCap_;
    // +0x198: retail vector<FRp8Adapter> (allocator byte, begin +0x19C, end +0x1A0, cap +0x1A4).
    TRp8Slot<std::vector<FRp8Adapter>, 0x10> Adapters_;
    unsigned long AdapterIndex_;
    unsigned long DeviceType_;
    char MgsdbPath_[0x80];
    unsigned long Field230_;
    unsigned long BbPfIndex_;
    int ModeBlob_[10];
    HWND Hwnd_;
    int WindowX_;
    int WindowY_;
    int WindowW_;
    int WindowH_;
    HWND WindowOverride_;
    // Startup properties, ability overrides and default device states.
    TRp8Slot<FRp8PropertyMap, 0x14> PropertyMap_;
    TRp8Slot<FRp8PropertyMap, 0x14> AbilityMap_;
    TRp8Slot<FRp8PropertyMap, 0x14> RenderStateMap_;
    TRp8Slot<FRp8PropertyMap, 0x14> StageStateMaps_[8];
    // +0x354: pipeline-state slots, 0xC per id {value, set, set-by-startup}.
    unsigned char Pad354_[0x378 - 0x354];
    // Pipeline-state entries are {value, hasDefault, overriddenByIni}.
    unsigned long TextureAllow8Bit_;   // this+0x378, pipeline-state id 3 value slot
    unsigned long Field37C_;           // this+0x37C, id 3 hasDefault slot
    unsigned long Field380_;           // this+0x380, id 3 overriddenByIni slot
    unsigned long TextureAllow32Bit_;  // this+0x384, pipeline-state id 4 value slot
    unsigned long Field388_;           // this+0x388, id 4 hasDefault slot ("Bad8888" trigger)
    unsigned long Field38C_;           // this+0x38C, id 4 overriddenByIni slot
    unsigned long TextureAllowDxt_;    // this+0x390, pipeline-state id 5 value slot
    unsigned long Field394_;           // this+0x394, id 5 hasDefault slot ("FL_BAD_DXTN" trigger)
    unsigned long Field398_;           // this+0x398, id 5 overriddenByIni slot
    unsigned char Pad39C_[0x3F0 - 0x39C];
    unsigned long BltNotFlip_;
    unsigned long Field3F4_;
    unsigned long Field3F8_;
    // Preserve the existing overlap between ability 20 and render-state entry 0.
    union
    {
        unsigned long Ability_[kAbilityCount];
        struct
        {
            unsigned char Pad3FC_[0x44C - 0x3FC];
            FRp8RenderStateCache RenderStates_;
        };
    };
    FRp8DeviceStateCaches DeviceStates_;
    FRp8LightManager LightManager_;
    FRp8PipelineTransforms Transforms_;
    D3DVIEWPORT8 ViewportCache_;
    unsigned char bDepthRangeValid_;
    unsigned char bViewportRectValid_;
    unsigned char Pad22E2_[0x22FC - 0x22E2];
    IDAComponent* Outer_;
    FRp8Inner Inner_;
};

static_assert(offsetof(FDirectX8, Parser_) == 0x20, "Parser +0x20");
static_assert(offsetof(FDirectX8, SectionName_) == 0x24, "SectionName +0x24");
static_assert(offsetof(FDirectX8, LastSection_) == 0xA4, "LastSection +0xA4");
static_assert(offsetof(FDirectX8, ScratchIbFlags_) == 0x14C, "ScratchIbFlags +0x14C");
static_assert(offsetof(FDirectX8, Adapters_) == 0x198, "Adapters +0x198");
static_assert(offsetof(FDirectX8, PropertyMap_) == 0x278, "PropertyMap +0x278");
static_assert(offsetof(FDirectX8, AbilityMap_) == 0x28C, "AbilityMap +0x28C");
static_assert(offsetof(FDirectX8, RenderStateMap_) == 0x2A0, "RenderStateMap +0x2A0");
static_assert(offsetof(FDirectX8, StageStateMaps_) == 0x2B4, "StageStateMaps +0x2B4");
static_assert(offsetof(FDirectX8, Pad354_) == 0x354, "pipeline-state slots +0x354");
static_assert(offsetof(FDirectX8, CreateFlags_) == 0x124, "CreateFlags +0x124");
static_assert(offsetof(FDirectX8, SwapFlags_) == 0x128, "SwapFlags +0x128");
static_assert(offsetof(FDirectX8, D3d_) == 0x12C, "D3d +0x12C");
static_assert(offsetof(FDirectX8, Device_) == 0x130, "Device +0x130");
static_assert(offsetof(FDirectX8, LockBufferSurf_) == 0x134, "LockBufferSurf +0x134");
static_assert(offsetof(FDirectX8, StreamCache_) == 0x138, "stream cache +0x138");
static_assert(offsetof(FDirectX8, ScratchIb_) == 0x150, "ScratchIb +0x150");
static_assert(offsetof(FDirectX8, VidMem_) == 0x168, "VidMem +0x168");
static_assert(offsetof(FDirectX8, VbPool_) == 0x16C, "VbPool +0x16C");
static_assert(offsetof(FDirectX8, VbPoolBytes_) == 0x180, "VbPoolBytes +0x180");
static_assert(offsetof(FDirectX8, DeviceReady_) == 0x184, "DeviceReady +0x184");
static_assert(offsetof(FDirectX8, FormatCatalogBegin_) == 0x18C, "FormatCatalogBegin +0x18C");
static_assert(offsetof(FDirectX8, FormatCatalogEnd_) == 0x190, "FormatCatalogEnd +0x190");
static_assert(offsetof(FDirectX8, FormatCatalogCap_) == 0x194, "FormatCatalogCap +0x194");
static_assert(offsetof(FDirectX8, AdapterIndex_) == 0x1A8, "AdapterIndex +0x1A8");
static_assert(offsetof(FDirectX8, DeviceType_) == 0x1AC, "DeviceType +0x1AC");
static_assert(offsetof(FDirectX8, MgsdbPath_) == 0x1B0, "MgsdbPath +0x1B0");
static_assert(offsetof(FDirectX8, Field230_) == 0x230, "Field230 +0x230");
static_assert(offsetof(FDirectX8, BbPfIndex_) == 0x234, "BbPfIndex +0x234");
static_assert(offsetof(FDirectX8, ModeBlob_) == 0x238, "ModeBlob +0x238");
static_assert(offsetof(FDirectX8, Hwnd_) == 0x260, "Hwnd +0x260");
static_assert(offsetof(FDirectX8, WindowX_) == 0x264, "WindowX +0x264");
static_assert(offsetof(FDirectX8, WindowOverride_) == 0x274, "WindowOverride +0x274");
static_assert(offsetof(FDirectX8, TextureAllow8Bit_) == 0x378, "TextureAllow8Bit +0x378");
static_assert(offsetof(FDirectX8, TextureAllow32Bit_) == 0x384, "TextureAllow32Bit +0x384");
static_assert(offsetof(FDirectX8, TextureAllowDxt_) == 0x390, "TextureAllowDxt +0x390");
static_assert(offsetof(FDirectX8, Ability_) == 0x3FC, "Ability +0x3FC");
static_assert(offsetof(FDirectX8, RenderStates_) == 0x44C, "RenderStates +0x44C");
static_assert(offsetof(FDirectX8, LightManager_) == 0x21F4, "LightManager +0x21F4");
static_assert(offsetof(FDirectX8, DeviceStates_) == 0x1854, "device-state caches +0x1854");
static_assert(offsetof(FDirectX8, Transforms_) == 0x21FC, "pipeline transforms +0x21FC");
static_assert(sizeof(D3DVIEWPORT8) == 0x18, "D3DVIEWPORT8 6 dword");
static_assert(offsetof(FDirectX8, ViewportCache_) == 0x22C8, "ViewportCache +0x22C8");
static_assert(offsetof(FDirectX8, bDepthRangeValid_) == 0x22E0, "bDepthRangeValid +0x22E0");
static_assert(offsetof(FDirectX8, bViewportRectValid_) == 0x22E1, "bViewportRectValid +0x22E1");
static_assert(offsetof(FDirectX8, Outer_) == 0x22FC, "Outer +0x22FC");
static_assert(offsetof(FDirectX8, Inner_) == 0x2300, "Inner +0x2300");
static_assert(sizeof(FDirectX8) == kRp8ObjectSize, "DirectX8 object 0x230C");
