// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpu.h"

#include "dacom_iids.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    // GENRESULT values RP8's callers test for.
    constexpr GENRESULT kUnsupported = (GENRESULT)0xFFFFFFF9; // the operation does not exist
    constexpr GENRESULT kFail = (GENRESULT)0x80004005;        // E_FAIL, the buffer calls

    constexpr unsigned long kRegisterPriority = 0x40000000; // RP8's
    constexpr unsigned int kScratchIbIndices = 0x4000;
    constexpr unsigned int kPoolGrowVertices = 0x400;

    // Handle -1: the scratch index buffer, or the pool vertex buffer of a source FVF.
    void* const kMinusOne = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));

    // Pipeline state ids (RP8's [RenderPipeline] keys).
    enum : int
    {
        kPsTexture = 0,
        kPsLighting = 1,
        kPsTextureLod = 2,
        kPsAllow8Bit = 3,
        kPsAllow32Bit = 4,
        kPsAllowDxt = 5,
        kPsMaxBlendStages = 6,
        kPsViewspaceLights = 7,
        kPsClearColor = 8,
        kPsClearZDepth = 9,
        kPsClearStencil = 10,
        kPsStateCache = 11,
        kPsBrokenMod2x = 12,
        kPsBrokenFlip = 13,
        kPsCount = 14,
    };

    FRpGpuFactory* G_Factory = nullptr;

    // RP8's aggregation record (FRp8AggDesc): the client's AGGDESC with the outer object, where
    // the inner controller goes, and the ini section. Pointer fields, so it holds on x64 too.
    struct FAggDesc
    {
        unsigned long Size;
        const char* Name;
        IDAComponent* Outer;
        IDAComponent** InnerOut;
        const char* Section;
    };

    bool IidEqual(const char* Left, const char* Right)
    {
        return Left != nullptr && Right != nullptr && std::strcmp(Left, Right) == 0;
    }

    void Log(const char* Format, ...)
    {
        char Text[512];
        va_list Args;
        va_start(Args, Format);
        std::vsnprintf(Text, sizeof(Text), Format, Args);
        va_end(Args);
        OutputDebugStringA(Text);
    }

    // CPU memory a caller may see through a 32-bit slot: below 2 GB on x64.
    unsigned char* AllocateLow(std::size_t Size)
    {
#if defined(_WIN64)
        return static_cast<unsigned char*>(LowArena::Allocate(Size));
#else
        return static_cast<unsigned char*>(std::malloc(Size));
#endif
    }

    void FreeLow(unsigned char* Bits, std::size_t Size)
    {
#if defined(_WIN64)
        LowArena::Free(Bits, Size);
#else
        (void)Size;
        std::free(Bits);
#endif
    }

    // RP8's FvfStride: bytes per vertex of a D3D8 flexible vertex format.
    unsigned int FvfStride(unsigned int Fvf)
    {
        unsigned int Bytes = 0;
        if ((Fvf & 0xC) == 0xC)
            Bytes = 0x1C;
        else if ((Fvf & 4) == 4)
            Bytes = 0x10;
        else if ((Fvf & 2) == 2)
            Bytes = 0xC;
        if ((Fvf & 0x10) != 0)
            Bytes += 0xC;
        if ((Fvf & 0x40) != 0)
            Bytes += 4;
        if ((Fvf & 0x80) != 0)
            Bytes += 4;
        const unsigned int TexCount = (Fvf & 0xF00) >> 8;
        for (unsigned int Index = 0; Index < TexCount; ++Index)
        {
            const unsigned int Size = (Fvf >> (Index * 2 + 0x10)) & 3;
            Bytes += Size == 0 ? 8 : Size == 1 ? 0xC : Size == 2 ? 0x10 : 4;
        }
        return Bytes;
    }

    // RP8 pixel-format numbers (pfenum): 1 P8, 2 R8G8B8, 3 R5G6B5, 4 X1R5G5B5, 5 A4R4G4B4,
    // 6 A1R5G5B5, 7 A8R8G8B8, 8 X8R8G8B8, 10..14 DXT1..5. 15..21 are format classes that RP8
    // resolves to one of those.
    unsigned int ConcreteFormat(unsigned int Format)
    {
        switch (Format)
        {
        case 15:
        case 16:
            return 3;
        case 19:
            return 6;
        case 20:
            return 5;
        case 17:
        case 18:
        case 21:
            return 7;
        default:
            return Format;
        }
    }

    bool IsKnownFormat(unsigned int Format)
    {
        return (Format >= 1 && Format <= 8) || (Format >= 10 && Format <= 14);
    }

    bool IsBlockFormat(unsigned int Format)
    {
        return Format >= 10 && Format <= 14;
    }

    unsigned int BytesPerPixel(unsigned int Format)
    {
        switch (Format)
        {
        case 1:
            return 1;
        case 2:
            return 3;
        case 3:
        case 4:
        case 5:
        case 6:
            return 2;
        default:
            return 4;
        }
    }

    // Bytes per row of blocks for DXT, per row of pixels otherwise.
    unsigned int RowPitch(unsigned int Format, unsigned int Width)
    {
        if (IsBlockFormat(Format))
            return std::max(1u, (Width + 3) / 4) * (Format == 10 ? 8u : 16u);
        return Width * BytesPerPixel(Format);
    }

    unsigned int RowCount(unsigned int Format, unsigned int Height)
    {
        return IsBlockFormat(Format) ? std::max(1u, (Height + 3) / 4) : Height;
    }

    FRpGpuImage MakeImage(unsigned int Format, unsigned int Width, unsigned int Height)
    {
        FRpGpuImage Image;
        Image.Width = std::max(1u, Width);
        Image.Height = std::max(1u, Height);
        Image.Pitch = RowPitch(Format, Image.Width);
        Image.Size = static_cast<std::size_t>(Image.Pitch) * RowCount(Format, Image.Height);
        Image.Bits = AllocateLow(Image.Size);
        if (Image.Bits != nullptr)
            std::memset(Image.Bits, 0, Image.Size);
        return Image;
    }

    void Identity(float* M)
    {
        std::memset(M, 0, 16 * sizeof(float));
        M[0] = M[5] = M[10] = M[15] = 1.0f;
    }

    void Transpose(const float* In, float* Out)
    {
        for (int Row = 0; Row < 4; ++Row)
            for (int Col = 0; Col < 4; ++Col)
                Out[Col * 4 + Row] = In[Row * 4 + Col];
    }

    // DDS file header fields GetImageInfo reads (offsets from the start of the file).
    std::uint32_t ReadU32(const unsigned char* Data, unsigned int Offset)
    {
        std::uint32_t Value;
        std::memcpy(&Value, Data + Offset, sizeof(Value));
        return Value;
    }
}

FRpGpuTexture::~FRpGpuTexture()
{
    for (FRpGpuImage& Image : Images)
        FreeLow(Image.Bits, Image.Size);
}

// --- aggregation -----------------------------------------------------------------------------

GENRESULT DACOM_API FRpGpuInner::QueryInterface(const char* Iid, void** Out)
{
    if (Owner == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    return Owner->QueryLocal(Iid, Out);
}

unsigned long DACOM_API FRpGpuInner::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FRpGpuInner::Release()
{
    if (RefCount > 0)
        --RefCount;
    if (RefCount != 0)
        return RefCount;
    // Stays referenced while the owner is torn down, as RP8's controller does.
    ++RefCount;
    delete Owner;
    return 0;
}

GENRESULT DACOM_API FRpGpuFactory::QueryInterface(const char* Iid, void** Out)
{
    if (Out == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    if (!IidEqual(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = static_cast<IComponentFactory*>(this);
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FRpGpuFactory::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FRpGpuFactory::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Left = RefCount;
    if (Left == 0)
    {
        if (G_Factory == this)
            G_Factory = nullptr;
        delete this;
    }
    return Left;
}

// The descriptor is RP8's 0x14-byte aggregation record: +0 size, +4 component name, +8 outer,
// +0xC where the inner controller goes, +0x10 the ini section (or null).
GENRESULT DACOM_API FRpGpuFactory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    FAggDesc* const Ext = reinterpret_cast<FAggDesc*>(Desc);
    if (Instance == nullptr)
        return GR_INVALID_PARMS;
    *Instance = nullptr;
    if (Ext == nullptr || Ext->Size != 0x14 || !IidEqual(Ext->Name, kRpGpuComponentName))
        return GR_INTERFACE_UNSUPPORTED;

    FRpGpu* const Object = new (std::nothrow) FRpGpu();
    if (Object == nullptr)
        return GR_OUT_OF_MEMORY;
    Object->Inner_.Owner = Object;
    Object->Outer_ = Ext->Outer;
    if (Object->Outer_ == nullptr)
        Object->Outer_ = &Object->Inner_;
    else if (Ext->InnerOut != nullptr)
        *Ext->InnerOut = &Object->Inner_;

    // RP8 only records the section here; Startup reads it.
    if (Ext->Section != nullptr && Ext->Section[0] != '\0')
    {
        std::strncpy(Object->Section_, Ext->Section, sizeof(Object->Section_) - 1);
        Object->Section_[sizeof(Object->Section_) - 1] = '\0';
    }

    *Instance = static_cast<IRenderPipeline*>(Object);
    return GR_OK;
}

// --- the component ---------------------------------------------------------------------------

FRpGpu::FRpGpu()
{
    std::strcpy(Section_, "RenderPipeline");
    ScratchIb_.Stride = 2;
    ScratchIb_.Count = kScratchIbIndices;
    ScratchIb_.bRing = true;
    ScratchIb_.Data.resize(static_cast<std::size_t>(kScratchIbIndices) * 2);
    Identity(Projection_);
}

FRpGpu::~FRpGpu()
{
    Device_.Destroy();
    for (FRpGpuTexture* Texture : Textures_)
        delete Texture;
    for (FRpGpuBuffer* Buffer : VertexBuffers_)
        delete Buffer;
    for (FRpGpuBuffer* Buffer : IndexBuffers_)
        delete Buffer;
    for (auto& Pool : VbPools_)
        delete Pool.second;
}

GENRESULT FRpGpu::QueryLocal(const char* Iid, void** Out)
{
    if (Out == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    IDAComponent* Face = nullptr;
    if (IidEqual(Iid, Iids::RenderPipeline8B))
        Face = static_cast<IRenderPipeline*>(this);
    else if (IidEqual(Iid, Iids::VertexBufferManager))
        Face = static_cast<IVertexBufferManager*>(this);
    else if (IidEqual(Iid, Iids::RPDraw))
        Face = static_cast<IRPDraw*>(this);
    else if (IidEqual(Iid, Iids::RPIndexBuffer))
        Face = static_cast<IRPIndexBuffer*>(this);
    else if (IidEqual(Iid, Iids::RPVertexBuffer))
        Face = static_cast<IRPVertexBuffer*>(this);
    else if (IidEqual(Iid, Iids::GammaControl))
        Face = static_cast<IGammaControl*>(this);
    else if (IidEqual(Iid, Iids::RPTexture))
        Face = static_cast<IRPTexture*>(this);
    else if (IidEqual(Iid, Iids::AggregateComponent))
        Face = static_cast<IAggregateComponent*>(this);
    if (Face == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::QueryInterface(const char* Iid, void** Out)
{
    if (Outer_ != nullptr && Outer_ != &Inner_)
        return Outer_->QueryInterface(Iid, Out);
    return QueryLocal(Iid, Out);
}

unsigned long DACOM_API FRpGpu::AddRef()
{
    return Outer_ != nullptr ? Outer_->AddRef() : Inner_.AddRef();
}

unsigned long DACOM_API FRpGpu::Release()
{
    return Outer_ != nullptr ? Outer_->Release() : Inner_.Release();
}

GENRESULT DACOM_API FRpGpu::Initialize()
{
    return GR_OK;
}

void FRpGpu::ResetDeviceState()
{
    std::memset(RenderStates_, 0, sizeof(RenderStates_));
    std::memset(StageStates_, 0, sizeof(StageStates_));
    // D3D8 defaults the game relies on: every stage disabled (COLOROP / ALPHAOP = DISABLE).
    for (int Stage = 0; Stage < 8; ++Stage)
    {
        StageStates_[Stage][1] = 1; // D3DTSS_COLOROP
        StageStates_[Stage][4] = 1; // D3DTSS_ALPHAOP
    }
    Identity(Projection_);
    Viewport_[0] = 0;
    Viewport_[1] = 0;
    Viewport_[2] = static_cast<int>(Device_.Width());
    Viewport_[3] = static_cast<int>(Device_.Height());
    DepthRange_[0] = 0.0f;
    DepthRange_[1] = 1.0f;
}

void FRpGpu::LoadModes()
{
    if (bModesLoaded_)
        return;
    bModesLoaded_ = true;
    std::vector<FRpGpuDisplayMode> Found;
    FRpGpuDevice::QueryDisplayModes(&Found);
    for (const FRpGpuDisplayMode& Mode : Found)
    {
        bool bSeen = false;
        for (const FMode& Known : Modes_)
            bSeen = bSeen || (Known.Width == Mode.Width && Known.Height == Mode.Height &&
                              Known.Refresh == Mode.Refresh);
        if (bSeen)
            continue;
        // Each size twice, as 16-bit and 32-bit: the options screen asks for one of the two.
        Modes_.push_back({Mode.Width, Mode.Height, Mode.Refresh, 3});
        Modes_.push_back({Mode.Width, Mode.Height, Mode.Refresh, 8});
    }
}

// --- IRenderPipeline -------------------------------------------------------------------------

GENRESULT DACOM_API FRpGpu::Startup(const char* SectionName)
{
    if (SectionName != nullptr && SectionName[0] != '\0')
    {
        std::strncpy(Section_, SectionName, sizeof(Section_) - 1);
        Section_[sizeof(Section_) - 1] = '\0';
    }
    PipelineState_[kPsTexture] = 1;
    PipelineState_[kPsLighting] = 1;
    PipelineState_[kPsTextureLod] = 1;
    PipelineState_[kPsAllow8Bit] = 0;
    PipelineState_[kPsAllow32Bit] = 1;
    PipelineState_[kPsAllowDxt] = 1;
    PipelineState_[kPsMaxBlendStages] = 7;
    PipelineState_[kPsViewspaceLights] = 1;
    PipelineState_[kPsClearColor] = 0;
    PipelineState_[kPsClearZDepth] = 0x3F800000; // 1.0f
    PipelineState_[kPsClearStencil] = -1;
    PipelineState_[kPsStateCache] = 1;
    PipelineState_[kPsBrokenMod2x] = 0;
    PipelineState_[kPsBrokenFlip] = 0;
    bStarted_ = true;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ShutdownPipeline()
{
    Device_.Destroy();
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetPipelineState(int Id, int Value)
{
    if (Id >= 0 && Id < kPsCount)
        PipelineState_[Id] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetPipelineState(int Id, int* Out)
{
    if (!bStarted_)
        return GR_GENERIC;
    if (Out != nullptr)
    {
        if (Id >= 0 && Id < kPsCount)
            *Out = PipelineState_[Id];
        else if (Id == kPsCount)
            *Out = 0;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetDeviceInfo(void* Out520)
{
    // No caller. A zeroed block of RP8's size.
    if (Out520 == nullptr)
        return GR_INVALID_PARMS;
    std::memset(Out520, 0, 0x520);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::QueryDeviceAbility(unsigned int Index, unsigned long* Out)
{
    if (!bStarted_)
        return GR_GENERIC;
    if (Index > 0x14 || Out == nullptr)
        return GR_INVALID_PARMS;
    unsigned long Value = 0;
    switch (Index)
    {
    case 3:
        Value = 0x20000000; // video memory: 512 MB, enough for the high-detail options
        break;
    case 4: // hardware transform and lighting
    case 9: // depth bias (shadows)
    case 10:
    case 11:
    case 12: // vertex / table / range fog
    case 13: // W fog
    case 16: // trilinear filtering
        Value = 1;
        break;
    case 6:
    case 7:
        Value = 4096; // texture width / height limit
        break;
    default:
        // 1 / 2 gamma (SDL has no gamma ramp), 5 square-only textures, 8 cube maps (not loaded
        // yet), 14 antialiased edges, 15 low-machine warnings, 17 / 18 device quirks.
        Value = 0;
        break;
    }
    *Out = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetDisplayModeCount(unsigned int* Out)
{
    LoadModes();
    if (Out != nullptr)
        *Out = static_cast<unsigned int>(Modes_.size());
    return GR_OK;
}

// Five dwords: index, width, height, refresh rate, pixel format (pfenum).
GENRESULT DACOM_API FRpGpu::GetDisplayModeEnum(unsigned int Index, void* Out)
{
    LoadModes();
    if (Out == nullptr || Index >= Modes_.size())
        return GR_GENERIC;
    unsigned long* const Record = static_cast<unsigned long*>(Out);
    const FMode& Mode = Modes_[Index];
    Record[0] = Index;
    Record[1] = Mode.Width;
    Record[2] = Mode.Height;
    Record[3] = Mode.Refresh;
    Record[4] = Mode.PixelFormat;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SelectMode(int* Mode, unsigned int* OutIndex)
{
    (void)Mode;
    if (OutIndex != nullptr)
        *OutIndex = 0xFFFFFFFFu;
    return GR_OK;
}

// Mode and OutMode are the first 0x28 bytes of the client's DisplaySettings: +0 mode index,
// +4 refresh, +8 width, +0xC height, +0x10 colour bits, +0x14 depth bits, +0x18 stencil bits,
// +0x1C present interval, +0x20 buffer count, +0x24 flip allowed, +0x25 full screen.
GENRESULT DACOM_API FRpGpu::CreateBuffers(HWND Hwnd, int* Mode, int* OutMode)
{
    if (!bStarted_ || Mode == nullptr)
        return GR_GENERIC;
    const unsigned char* const ModeBytes = reinterpret_cast<const unsigned char*>(Mode);
    const int Interval = Mode[7];
    if (!Device_.Create(Hwnd, Interval != 0))
    {
        Log("RPGPU: no SDL_GPU device for the window: %s\n", Device_.LastError());
        return GR_GENERIC;
    }
    if (OutMode != nullptr)
    {
        int Result[10] = {};
        Result[0] = -1;
        Result[1] = 0;
        Result[2] = Mode[2];
        Result[3] = Mode[3];
        Result[4] = Mode[4] == 16 ? 16 : 32;
        Result[5] = 24;
        Result[6] = Mode[6] == 0 ? 0 : 8;
        Result[7] = Interval;
        Result[8] = std::max(1, Mode[8]);
        std::memcpy(OutMode, Result, sizeof(Result));
        unsigned char* const OutBytes = reinterpret_cast<unsigned char*>(OutMode);
        OutBytes[0x24] = 1;
        OutBytes[0x25] = ModeBytes[0x25];
        OutBytes[0x26] = 0;
        OutBytes[0x27] = 0;
    }
    ResetDeviceState();
    if (Viewport_[2] == 0)
    {
        Viewport_[2] = Mode[2];
        Viewport_[3] = Mode[3];
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetModeBlob(void* OutAdapter, void* OutMode10Dwords)
{
    if (OutAdapter != nullptr)
        *static_cast<unsigned long*>(OutAdapter) = 0;
    if (OutMode10Dwords != nullptr)
        std::memset(OutMode10Dwords, 0, 0x28);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyGpuResources()
{
    return GR_OK;
}

// Flags: 1 colour (pipeline state 8), 2 depth (9), 4 stencil (10). The skeleton draws nothing,
// so only the colour matters: the next present clears to it.
GENRESULT DACOM_API FRpGpu::ClearBuffers(unsigned long Flags, const void* Rects)
{
    (void)Rects;
    if ((Flags & 1) != 0)
        Device_.SetClearColor(static_cast<std::uint32_t>(PipelineState_[kPsClearColor]));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SwapBuffers()
{
    if (!Device_.IsReady())
        return GR_GENERIC;
    if (!Device_.Present())
        Log("RPGPU: present failed: %s\n", Device_.LastError());
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::LockBuffer(void* Out)
{
    (void)Out;
    return GR_GENERIC; // RP8 too: its back buffer is not lockable
}

GENRESULT DACOM_API FRpGpu::UnlockBuffer()
{
    return GR_GENERIC;
}

GENRESULT DACOM_API FRpGpu::GetBufferInterface(void* Out, void* Unused)
{
    (void)Out;
    (void)Unused;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::GetDeviceStats(void* Out)
{
    (void)Out;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::SetViewportRect(int X, int Y, int W, int H)
{
    Viewport_[0] = X;
    Viewport_[1] = Y;
    Viewport_[2] = W;
    Viewport_[3] = H;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetViewportRect(int* OutX, int* OutY, int* OutW, int* OutH)
{
    if (OutX != nullptr)
        *OutX = Viewport_[0];
    if (OutY != nullptr)
        *OutY = Viewport_[1];
    if (OutW != nullptr)
        *OutW = Viewport_[2];
    if (OutH != nullptr)
        *OutH = Viewport_[3];
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetDepthRange(float MinZ, float MaxZ)
{
    DepthRange_[0] = MinZ;
    DepthRange_[1] = MaxZ;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetDepthRange(float* OutMinZ, float* OutMaxZ)
{
    if (OutMinZ != nullptr)
        *OutMinZ = DepthRange_[0];
    if (OutMaxZ != nullptr)
        *OutMaxZ = DepthRange_[1];
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetWindow(HWND Hwnd, int X, int Y, int W, int H)
{
    (void)Hwnd;
    (void)X;
    (void)Y;
    (void)W;
    (void)H;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetWindow(HWND* OutHwnd, int* OutLeft, int* OutRight, int* OutW, int* OutH)
{
    if (OutHwnd != nullptr)
        *OutHwnd = Device_.Hwnd();
    if (OutLeft != nullptr)
        *OutLeft = 0;
    if (OutRight != nullptr)
        *OutRight = 0;
    if (OutW != nullptr)
        *OutW = static_cast<int>(Device_.Width());
    if (OutH != nullptr)
        *OutH = static_cast<int>(Device_.Height());
    return GR_OK;
}

// The world, view and model-view transforms have no reader yet; the skeleton draws nothing.
GENRESULT DACOM_API FRpGpu::SetWorld(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetWorld(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetView(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetView(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetModelview(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetModelview(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
    return GR_OK;
}

// The projection is kept as D3D's row-major matrix; callers hand it over and read it back
// transposed (RP8's convention).
GENRESULT DACOM_API FRpGpu::SetProjection(const float* M16)
{
    if (M16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(M16, Projection_);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetProjection(float* Out16)
{
    if (Out16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(Projection_, Out16);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                      float AtZ, float UpX, float UpY, float UpZ)
{
    (void)EyeX;
    (void)EyeY;
    (void)EyeZ;
    (void)AtX;
    (void)AtY;
    (void)AtZ;
    (void)UpX;
    (void)UpY;
    (void)UpZ;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetOrtho(float Left, float Right, float Bottom, float Top, float NearPlane,
                                     float FarPlane)
{
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = 2.0f / (Right - Left);
    Projection_[5] = 2.0f / (Top - Bottom);
    Projection_[10] = 2.0f / (FarPlane - NearPlane);
    Projection_[12] = -(Right + Left) / (Right - Left);
    Projection_[13] = -(Bottom + Top) / (Top - Bottom);
    Projection_[14] = -NearPlane / (FarPlane - NearPlane);
    Projection_[15] = 1.0f;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane)
{
    if (NearPlane <= 0.0f || FarPlane <= 0.0f || FarPlane == NearPlane)
        return GR_GENERIC;
    const float C = 1.0f / std::tan(FovDeg * 3.14159265358979f / 180.0f);
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = C / Aspect;
    Projection_[5] = C;
    Projection_[10] = FarPlane / (FarPlane - NearPlane);
    Projection_[11] = 1.0f;
    Projection_[14] = -FarPlane * NearPlane / (FarPlane - NearPlane);
    return GR_OK;
}

// Lights: the handle is the caller's own D3DLIGHT8. Nothing is lit until the skeleton draws.
GENRESULT DACOM_API FRpGpu::SetLight(void* Handle, const D3DLIGHT8* Light)
{
    (void)Handle;
    (void)Light;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetLight(void* Handle, D3DLIGHT8* OutLight)
{
    (void)Handle;
    if (OutLight != nullptr)
        std::memset(OutLight, 0, sizeof(*OutLight));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetLightEnable(void* Handle, int Enable)
{
    (void)Handle;
    (void)Enable;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetLightEnable(void* Handle, int* OutEnable)
{
    (void)Handle;
    if (OutEnable != nullptr)
        *OutEnable = 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetMaterial(const D3DMATERIAL8* Material)
{
    (void)Material;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetMaterial(D3DMATERIAL8* Material)
{
    if (Material != nullptr)
        std::memset(Material, 0, sizeof(*Material));
    return GR_OK;
}

// --- textures --------------------------------------------------------------------------------

FRpGpuTexture* FRpGpu::LiveTexture(void* Handle) const
{
    FRpGpuTexture* const Texture = static_cast<FRpGpuTexture*>(Handle);
    return Textures_.count(Texture) != 0 ? Texture : nullptr;
}

GENRESULT DACOM_API FRpGpu::CreateTexture(int Width, int Height, unsigned int* Format, unsigned int Levels,
                                          unsigned int Flags, void** OutHandle)
{
    if ((Flags & 1) != 0 || Format == nullptr || OutHandle == nullptr || Width <= 0 || Height <= 0)
        return GR_INVALID_PARMS;
    const unsigned int Concrete = ConcreteFormat(*Format);
    if (!IsKnownFormat(Concrete))
        return GR_GENERIC;

    FRpGpuTexture* const Texture = new (std::nothrow) FRpGpuTexture();
    if (Texture == nullptr)
        return GR_OUT_OF_MEMORY;
    Texture->Width = static_cast<unsigned int>(Width);
    Texture->Height = static_cast<unsigned int>(Height);
    Texture->Format = Concrete;
    unsigned int Count = Levels;
    if (Count == 0)
    {
        // A full chain down to 1x1.
        Count = 1;
        for (unsigned int Size = std::max(Texture->Width, Texture->Height); Size > 1; Size >>= 1)
            ++Count;
    }
    Texture->Levels = Count;
    for (unsigned int Level = 0; Level < Count; ++Level)
    {
        FRpGpuImage Image = MakeImage(Concrete, Texture->Width >> Level, Texture->Height >> Level);
        if (Image.Bits == nullptr)
        {
            delete Texture;
            return GR_OUT_OF_MEMORY;
        }
        Texture->Images.push_back(Image);
    }
    Textures_.insert(Texture);
    *OutHandle = Texture;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyTexture(void* Handle)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture != nullptr)
    {
        Textures_.erase(Texture);
        delete Texture;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::IsTexture(void* Handle)
{
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_GENERIC;
}

// Out: five dwords: width, height, pitch in bytes, bits, pixel format (pfenum).
GENRESULT DACOM_API FRpGpu::LockTexture(void* Handle, int Level, void* Out)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr || Out == nullptr)
        return GR_INVALID_PARMS;
    if (Level < 0 || static_cast<std::size_t>(Level) >= Texture->Images.size())
        return GR_GENERIC;
    const FRpGpuImage& Image = Texture->Images[static_cast<std::size_t>(Level)];
    unsigned long* const Record = static_cast<unsigned long*>(Out);
    Record[0] = Image.Width;
    Record[1] = Image.Height;
    Record[2] = Image.Pitch;
    // Retail's record keeps the address in a dword; LowArena keeps it below 2 GB on x64.
    Record[3] = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(Image.Bits));
    Record[4] = Texture->Format;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockTexture(void* Handle, int Level)
{
    (void)Level;
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_INVALID_PARMS;
}

GENRESULT DACOM_API FRpGpu::GetTextureFormat(void* Handle, int* OutFormat)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr)
        return GR_INVALID_PARMS;
    if (OutFormat != nullptr)
        *OutFormat = static_cast<int>(Texture->Format);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH, int* OutLevels)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr)
        return GR_INVALID_PARMS;
    if (OutW != nullptr)
        *OutW = Texture->Width;
    if (OutH != nullptr)
        *OutH = Texture->Height;
    if (OutLevels != nullptr)
        *OutLevels = Texture->bCube ? 6 : static_cast<int>(Texture->Levels);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureInterface(void* Handle, void* UnusedA, void* UnusedB)
{
    (void)Handle;
    (void)UnusedA;
    (void)UnusedB;
    return kUnsupported;
}

// The loaders (Shading's DDS and TGA readers, ximage) fill a level from their own pixels. The
// skeleton keeps the level's memory but does not convert: nothing samples it yet.
GENRESULT DACOM_API FRpGpu::SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH, unsigned int Pitch,
                                                unsigned int* SrcFormat, const void* Pixels, void* A, void* B)
{
    (void)SrcW;
    (void)SrcH;
    (void)Pitch;
    (void)Pixels;
    (void)A;
    (void)B;
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr || SrcFormat == nullptr || *SrcFormat == 0)
        return GR_INVALID_PARMS;
    if (Level > 0 && PipelineState_[kPsTextureLod] == 0)
        return GR_OK;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::BlitTexture(void* DstHandle, int DstLevel, void* SrcHandle, int SrcLevel)
{
    (void)DstHandle;
    (void)DstLevel;
    (void)SrcHandle;
    (void)SrcLevel;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetRenderTarget(void* Handle, void* UnusedA, void* UnusedB)
{
    (void)Handle;
    (void)UnusedA;
    (void)UnusedB;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::GetRenderTarget(void* Handle)
{
    (void)Handle;
    return kUnsupported;
}

// --- frame and state -------------------------------------------------------------------------

GENRESULT DACOM_API FRpGpu::BeginScene()
{
    return Device_.IsReady() ? GR_OK : GR_GENERIC;
}

GENRESULT DACOM_API FRpGpu::EndScene()
{
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResetRenderStatesToDefaults()
{
    std::memset(RenderStates_, 0, sizeof(RenderStates_));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetRenderState(D3DRENDERSTATETYPE State, int Value)
{
    const unsigned int Index = static_cast<unsigned int>(State);
    if (Index < 256)
        RenderStates_[Index] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetRenderState(D3DRENDERSTATETYPE State, int* Out)
{
    const unsigned int Index = static_cast<unsigned int>(State);
    if (Out != nullptr)
        *Out = Index < 256 ? RenderStates_[Index] : 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetTextureStageState(int Stage, int Type, int Value)
{
    if (Stage >= 0 && Stage < 8 && Type >= 0 && Type < 32)
        StageStates_[Stage][Type] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageState(int Stage, int Type, int* Out)
{
    if (Out != nullptr)
        *Out = (Stage >= 0 && Stage < 8 && Type >= 0 && Type < 32) ? StageStates_[Stage][Type] : 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetTextureStageTransform(int Stage, const float* Matrix16)
{
    (void)Stage;
    (void)Matrix16;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTransform(int Stage, float* OutMatrix16)
{
    (void)Stage;
    if (OutMatrix16 != nullptr)
        Identity(OutMatrix16);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetTextureStageTexture(int Stage, void* Handle)
{
    (void)Stage;
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTexture(int Stage, void** OutHandle)
{
    (void)Stage;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::VerifyState()
{
    return GR_OK;
}

// The draw calls: accepted and dropped until RPGPU draws (plan step 5.3).
GENRESULT DACOM_API FRpGpu::DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                          unsigned int Count, void* Unused)
{
    (void)Prim;
    (void)Fvf;
    (void)Verts;
    (void)Count;
    (void)Unused;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                                   unsigned int VertCount, const void* Indices,
                                                   unsigned int IndexCount, unsigned long Unused)
{
    (void)Prim;
    (void)Fvf;
    (void)Verts;
    (void)VertCount;
    (void)Indices;
    (void)IndexCount;
    (void)Unused;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex,
                                            unsigned int Count, void* Unused)
{
    (void)Prim;
    (void)Vb;
    (void)StartVertex;
    (void)Count;
    (void)Unused;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb, unsigned int BaseVertex,
                                                   unsigned int NumVerts, const void* Indices,
                                                   unsigned int IndexCount, unsigned long Unused)
{
    (void)Prim;
    (void)Vb;
    (void)BaseVertex;
    (void)NumVerts;
    (void)Indices;
    (void)IndexCount;
    (void)Unused;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::AddLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::RemoveLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UpdateLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetWorldN(int WorldIndex, const float* M12)
{
    (void)WorldIndex;
    (void)M12;
    return GR_OK;
}

// --- buffers ---------------------------------------------------------------------------------

// A ring lock: Count elements from the cursor, wrapping to 0 when they do not fit; a pool
// grows when a single lock is larger than the whole ring.
bool FRpGpu::LockRing(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int* OutStart)
{
    if (Count > Buffer->Count)
    {
        if (Buffer->Fvf == 0)
            return false; // the scratch index buffer has a fixed size
        const unsigned int Grown = (Count + kPoolGrowVertices - 1) / kPoolGrowVertices * kPoolGrowVertices;
        Buffer->Count = Grown;
        Buffer->Data.assign(static_cast<std::size_t>(Grown) * Buffer->Stride, 0);
        Buffer->Cursor = 0;
    }
    if (Buffer->Cursor + Count > Buffer->Count)
        Buffer->Cursor = 0;
    *OutStart = Buffer->Cursor;
    Buffer->Cursor += Count;
    return true;
}

FRpGpuBuffer* FRpGpu::PoolBuffer(unsigned int Fvf)
{
    auto Found = VbPools_.find(Fvf);
    if (Found != VbPools_.end())
        return Found->second;
    FRpGpuBuffer* const Pool = new (std::nothrow) FRpGpuBuffer();
    if (Pool == nullptr)
        return nullptr;
    Pool->Fvf = Fvf;
    Pool->Stride = std::max(1u, FvfStride(Fvf));
    Pool->Count = kPoolGrowVertices;
    Pool->bRing = true;
    Pool->Data.assign(static_cast<std::size_t>(Pool->Count) * Pool->Stride, 0);
    VbPools_[Fvf] = Pool;
    return Pool;
}

FRpGpuBuffer* FRpGpu::LiveVb(void* Handle) const
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    if (VertexBuffers_.count(Buffer) != 0)
        return Buffer;
    for (const auto& Pool : VbPools_)
    {
        if (Pool.second == Buffer)
            return Buffer;
    }
    return nullptr;
}

FRpGpuBuffer* FRpGpu::LiveIb(void* Handle) const
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    return IndexBuffers_.count(Buffer) != 0 ? Buffer : nullptr;
}

GENRESULT DACOM_API FRpGpu::VbMgrInit(void* A, void* B, void* C, void* D)
{
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::VbMgrReleaseAll()
{
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* const Pool = PoolBuffer(Fvf);
    unsigned int Start = 0;
    if (Pool == nullptr || !LockRing(Pool, Count, &Start))
        return GR_OUT_OF_MEMORY;
    Out->Listing.VertexBuffer = Pool;
    Out->Listing.Fvf = Fvf;
    Out->Listing.BaseVertex = Start;
    Out->Vertices = Pool->Data.data() + static_cast<std::size_t>(Start) * Pool->Stride;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockVertexMemory(FVertexMemoryLock* Out)
{
    if (Out == nullptr || Out->Listing.VertexBuffer == nullptr)
        return GR_INVALID_PARMS;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnsupportedManagerOperation()
{
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::CopyVertexData(void* Dest, unsigned int Fvf, const FRpVertexSrcDesc* Source,
                                           unsigned int StartVertex, unsigned int Count)
{
    // RP8 converts the source streams into Dest. Nothing reads the vertices yet.
    (void)Dest;
    (void)Fvf;
    (void)Source;
    (void)StartVertex;
    (void)Count;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex, unsigned int NumVertices,
                                                 unsigned int StartIndex, unsigned int Count)
{
    (void)Prim;
    (void)MinIndex;
    (void)NumVertices;
    (void)StartIndex;
    (void)Count;
    return GR_OK;
}

// Index buffers. Handle -1 is the scratch ring (0x4000 indices) alchemy draws from.
GENRESULT DACOM_API FRpGpu::CreateIb(unsigned int Count, unsigned int** Out, unsigned int Flags)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* const Buffer = new (std::nothrow) FRpGpuBuffer();
    if (Buffer == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Buffer->Stride = 2;
    Buffer->Count = std::max(1u, Count);
    Buffer->bRing = (Flags & 1) != 0;
    Buffer->Data.assign(static_cast<std::size_t>(Buffer->Count) * 2, 0);
    IndexBuffers_.insert(Buffer);
    *Out = reinterpret_cast<unsigned int*>(Buffer);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyIb(void** Handle)
{
    if (Handle == nullptr || *Handle == kMinusOne)
        return GR_GENERIC;
    FRpGpuBuffer* const Buffer = LiveIb(*Handle);
    if (Buffer != nullptr)
    {
        IndexBuffers_.erase(Buffer);
        delete Buffer;
    }
    *Handle = nullptr;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResizeIb(void* Handle, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = LiveIb(Handle);
    if (Buffer == nullptr)
        return kFail;
    Buffer->Count = std::max(1u, Count);
    Buffer->Data.assign(static_cast<std::size_t>(Buffer->Count) * 2, 0);
    Buffer->Cursor = 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData,
                                        unsigned int Count)
{
    void* Bits = nullptr;
    const GENRESULT Result = LockIb(Handle, OutOffset, &Bits, Count);
    if (Result != GR_OK)
        return Result;
    if (SrcData != nullptr && Bits != nullptr)
        std::memcpy(Bits, SrcData, static_cast<std::size_t>(Count) * 2);
    return UnlockIb(Handle);
}

// Offset is in indices, in and out. A dynamic buffer ignores the input and returns its ring
// position.
GENRESULT DACOM_API FRpGpu::LockIb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (Buffer == nullptr || Count > Buffer->Count || Out == nullptr)
        return kFail;
    unsigned int Start = Offset != nullptr ? *Offset : 0;
    if (Buffer->bRing)
    {
        if (!LockRing(Buffer, Count, &Start))
            return kFail;
    }
    else if (Start + Count > Buffer->Count)
    {
        return kFail;
    }
    if (Offset != nullptr)
        *Offset = Start;
    Buffer->bLocked = true;
    *Out = Buffer->Data.data() + static_cast<std::size_t>(Start) * 2;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockIb(void* Handle)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (Buffer == nullptr)
        return kFail;
    Buffer->bLocked = false;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SelectIb(void* Handle, unsigned int BaseVertexIndex, unsigned int Unused3,
                                     unsigned int Unused4)
{
    (void)BaseVertexIndex;
    (void)Unused3;
    (void)Unused4;
    return (Handle == kMinusOne || LiveIb(Handle) != nullptr) ? GR_OK : kFail;
}

GENRESULT DACOM_API FRpGpu::GetIbCapacity(void* Handle, unsigned int* OutCapacity)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (OutCapacity != nullptr)
        *OutCapacity = Buffer != nullptr ? Buffer->Count : 0;
    return GR_OK;
}

// 1 when valid, 0 otherwise: Deformable2 re-creates its buffer on 0.
GENRESULT DACOM_API FRpGpu::IsIbValid(void* Handle)
{
    return (Handle == kMinusOne || LiveIb(Handle) != nullptr) ? 1 : 0;
}

// Vertex buffers. Flags & 1 hands out the shared pool buffer of that FVF.
GENRESULT DACOM_API FRpGpu::CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out, unsigned int Flags)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* Buffer = nullptr;
    if ((Flags & 1) != 0)
    {
        Buffer = PoolBuffer(Fvf);
    }
    else
    {
        Buffer = new (std::nothrow) FRpGpuBuffer();
        if (Buffer != nullptr)
        {
            Buffer->Fvf = Fvf;
            Buffer->Stride = std::max(1u, FvfStride(Fvf));
            Buffer->Count = std::max(1u, Count);
            Buffer->Data.assign(static_cast<std::size_t>(Buffer->Count) * Buffer->Stride, 0);
            VertexBuffers_.insert(Buffer);
        }
    }
    *Out = reinterpret_cast<unsigned int*>(Buffer);
    return Buffer != nullptr ? GR_OK : GR_OUT_OF_MEMORY;
}

GENRESULT DACOM_API FRpGpu::DestroyVb(void** Handle)
{
    if (Handle == nullptr)
        return GR_GENERIC;
    if (*Handle == kMinusOne)
        return GR_OK;
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(*Handle);
    if (VertexBuffers_.count(Buffer) != 0)
    {
        VertexBuffers_.erase(Buffer);
        delete Buffer;
    }
    *Handle = nullptr; // a pool buffer stays alive
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    if (VertexBuffers_.count(Buffer) != 0)
    {
        Buffer->Fvf = Fvf;
        Buffer->Stride = std::max(1u, FvfStride(Fvf));
        Buffer->Count = std::max(1u, Count);
        Buffer->Data.assign(static_cast<std::size_t>(Buffer->Count) * Buffer->Stride, 0);
        Buffer->Cursor = 0;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* SrcData,
                                         unsigned int StartVertex, unsigned int Count)
{
    (void)StartVertex;
    if (Handle == nullptr)
        return GR_GENERIC;
    void* Target = Handle;
    if (Handle == kMinusOne)
    {
        if (SrcData == nullptr)
            return GR_GENERIC;
        Target = PoolBuffer(SrcData->SrcFvf);
    }
    FRpGpuBuffer* const Buffer = LiveVb(Target);
    if (Buffer == nullptr)
        return GR_GENERIC;
    if (Buffer->bLocked)
        return GR_OK; // the offset is already a vertex index
    // The vertices are not converted yet: nothing reads them.
    void* Bits = nullptr;
    unsigned int Offset = OutOffset != nullptr ? *OutOffset * Buffer->Stride : 0;
    const GENRESULT Result = LockVb(Target, &Offset, &Bits, Count);
    if (Result != GR_OK)
        return Result;
    if (OutOffset != nullptr)
        *OutOffset = Offset;
    return UnlockVb(Target);
}

// Offset goes in as bytes and comes back as a vertex index. Pool buffers ignore the input and
// return their ring position.
GENRESULT DACOM_API FRpGpu::LockVb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count)
{
    if (Handle == nullptr || Handle == kMinusOne || Out == nullptr)
        return kFail;
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (Buffer == nullptr)
        return kFail;
    unsigned int Start = 0;
    if (Buffer->bRing)
    {
        if (!LockRing(Buffer, Count, &Start))
            return kFail;
    }
    else
    {
        if (Count > Buffer->Count)
            return kFail;
        Start = (Offset != nullptr ? *Offset : 0) / Buffer->Stride;
        if (Start + Count > Buffer->Count)
            return kFail;
    }
    if (Offset != nullptr)
        *Offset = Start;
    Buffer->bLocked = true;
    *Out = Buffer->Data.data() + static_cast<std::size_t>(Start) * Buffer->Stride;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockVb(void* Handle)
{
    if (Handle == nullptr || Handle == kMinusOne)
        return kFail;
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (Buffer == nullptr)
        return kFail;
    Buffer->bLocked = false;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnsupportedVertexBufferOperation(void* Handle)
{
    (void)Handle;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::SelectVb(void* Handle)
{
    return LiveVb(Handle) != nullptr ? GR_OK : kFail;
}

GENRESULT DACOM_API FRpGpu::GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount)
{
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (OutFvf != nullptr)
        *OutFvf = Buffer != nullptr ? Buffer->Fvf : 0;
    if (OutCount != nullptr)
        *OutCount = Buffer != nullptr ? Buffer->Count : 0;
    return GR_OK;
}

// 1 when valid (or the pool handle -1), 0 otherwise.
GENRESULT DACOM_API FRpGpu::IsVbValid(void* Handle)
{
    if (Handle == kMinusOne)
        return 1;
    return LiveVb(Handle) != nullptr ? 1 : 0;
}

// --- IGammaControl: SDL3 has no gamma ramp, so ability 1 is 0 and these say "unsupported" ----

GENRESULT DACOM_API FRpGpu::SetGammaFunction(unsigned int ChannelMask, float Gamma, float Brightness,
                                             float Contrast, float Bias)
{
    (void)ChannelMask;
    (void)Gamma;
    (void)Brightness;
    (void)Contrast;
    (void)Bias;
    return GR_INTERFACE_UNSUPPORTED;
}

GENRESULT DACOM_API FRpGpu::SetGammaRamp(unsigned int ChannelMask, const unsigned short* RampData)
{
    (void)ChannelMask;
    (void)RampData;
    return GR_INTERFACE_UNSUPPORTED;
}

GENRESULT DACOM_API FRpGpu::GetGammaRamp(unsigned int Channel, unsigned short* OutRamp)
{
    (void)Channel;
    (void)OutRamp;
    return GR_INTERFACE_UNSUPPORTED;
}

GENRESULT DACOM_API FRpGpu::SetGammaEnable(unsigned char Enable)
{
    (void)Enable;
    return GR_INTERFACE_UNSUPPORTED;
}

GENRESULT DACOM_API FRpGpu::GetGammaEnable()
{
    return GR_INTERFACE_UNSUPPORTED;
}

// --- IRPTexture ------------------------------------------------------------------------------

GENRESULT DACOM_API FRpGpu::PrintScreen(void* ParentFileSystem, const char* FileName)
{
    (void)ParentFileSystem;
    (void)FileName;
    return GR_GENERIC;
}

GENRESULT DACOM_API FRpGpu::LoadTexture(IFileSystem* FileSystem, const char* FileName, void** OutHandle)
{
    (void)FileSystem;
    (void)FileName;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return GR_GENERIC; // no caller
}

GENRESULT DACOM_API FRpGpu::LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize, const void* SrcData)
{
    (void)Level;
    (void)SrcSize;
    (void)SrcData;
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_INVALID_PARMS;
}

// D3DXIMAGE_INFO for a DDS file, read from its header: +0 width, +4 height, +8 depth,
// +0xC mip levels, +0x10 D3DFORMAT, +0x14 resource type, +0x18 file format (4, DDS). Shading
// loads only DDS this way; anything else fails as D3DX's answer would make it.
GENRESULT DACOM_API FRpGpu::GetImageInfo(_D3DXIMAGE_INFO* Info, unsigned int SrcSize, const void* SrcData)
{
    const unsigned char* const Data = static_cast<const unsigned char*>(SrcData);
    if (Info == nullptr || Data == nullptr || SrcSize < 128 || std::memcmp(Data, "DDS ", 4) != 0)
        return GR_GENERIC;
    const std::uint32_t Height = ReadU32(Data, 12);
    const std::uint32_t Width = ReadU32(Data, 16);
    const std::uint32_t Depth = ReadU32(Data, 24);
    const std::uint32_t Mips = ReadU32(Data, 28);
    const std::uint32_t PfFlags = ReadU32(Data, 80);
    const std::uint32_t FourCc = ReadU32(Data, 84);
    const std::uint32_t Bits = ReadU32(Data, 88);
    const std::uint32_t RMask = ReadU32(Data, 92);
    const std::uint32_t GMask = ReadU32(Data, 96);
    const std::uint32_t AMask = ReadU32(Data, 104);
    const std::uint32_t Caps2 = ReadU32(Data, 112);

    std::uint32_t Format = 0;
    if ((PfFlags & 0x4) != 0) // DDPF_FOURCC: D3DFMT_DXT1..5 are the FourCC codes themselves
        Format = FourCc;
    else if ((PfFlags & 0x20) != 0) // DDPF_PALETTEINDEXED8
        Format = 41;                // D3DFMT_P8
    else if (Bits == 32)
        Format = AMask != 0 ? 21u : 22u; // A8R8G8B8 / X8R8G8B8
    else if (Bits == 24)
        Format = 20; // R8G8B8
    else if (Bits == 16)
    {
        if (GMask == 0x07E0)
            Format = 23; // R5G6B5
        else if (RMask == 0x0F00)
            Format = 26; // A4R4G4B4
        else
            Format = AMask != 0 ? 25u : 24u; // A1R5G5B5 / X1R5G5B5
    }
    if (Format == 0)
        return GR_GENERIC;

    std::uint32_t* const Out = reinterpret_cast<std::uint32_t*>(Info);
    Out[0] = Width;
    Out[1] = Height;
    Out[2] = (Caps2 & 0x200000) != 0 && Depth != 0 ? Depth : 1;
    Out[3] = Mips != 0 ? Mips : 1;
    Out[4] = Format;
    Out[5] = (Caps2 & 0x200) != 0 ? 5u : (Caps2 & 0x200000) != 0 ? 4u : 3u; // cube / volume / 2D
    Out[6] = 4; // D3DXIFF_DDS
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::LoadCubemap(IFileSystem* FileSystem, const char* FileName, void** OutHandle)
{
    // Ability 8 (cube maps) is 0: RP8 answers this before touching the file system.
    (void)FileSystem;
    (void)FileName;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return kUnsupported;
}

// --- registration ----------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(Module);
    ICOManager* const Dacom = DACOM_Acquire();
    if (Dacom == nullptr)
        return TRUE;
    G_Factory = new (std::nothrow) FRpGpuFactory();
    if (G_Factory == nullptr)
        return TRUE;
    Dacom->RegisterComponent(G_Factory, kRpGpuComponentName,
                             reinterpret_cast<void*>(static_cast<std::uintptr_t>(kRegisterPriority)));
    G_Factory->Release();
    return TRUE;
}
