// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's DACOM side (docs/re/rpgpu.md): the factory and aggregation, the interface lookup, the
// device and frame calls that go to the backend, the draw calls and DllMain. The state calls are
// in RpGpuState.cpp, the texture and buffer calls in RpGpuResources.cpp.

#include "RpGpu.h"

#include "dacom_iids.h"
#include "render/rp8_semantics.h" // Rp8FvfStride

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

using namespace RpGpuDetail;

namespace
{
    constexpr unsigned long kRegisterPriority = 0x40000000; // RP8's
    constexpr unsigned int kScratchIbIndices = 0x4000;

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

    void PutU16(unsigned char* Out, std::uint16_t Value)
    {
        Out[0] = static_cast<unsigned char>(Value & 0xFF);
        Out[1] = static_cast<unsigned char>(Value >> 8);
    }

    void PutU32(unsigned char* Out, std::uint32_t Value)
    {
        for (int Byte = 0; Byte < 4; ++Byte)
            Out[Byte] = static_cast<unsigned char>((Value >> (Byte * 8)) & 0xFF);
    }

    // A 24-bit BMP of the frame (rows bottom to top, padded to 4 bytes), as D3DX saves a
    // surface to .BMP.
    bool WriteBmp(const char* FileName, const FRpGpuFrame& Frame)
    {
        if (Frame.Width == 0 || Frame.Height == 0 || Frame.Width > 0x8000 || Frame.Height > 0x8000 ||
            Frame.Bgra.size() < static_cast<std::size_t>(Frame.Width) * Frame.Height * 4)
            return false;
        const std::uint32_t RowBytes = (Frame.Width * 3 + 3) & ~3u;
        const std::uint32_t ImageBytes = RowBytes * Frame.Height;
        unsigned char Header[54] = {};
        Header[0] = 'B';
        Header[1] = 'M';
        PutU32(Header + 2, 54 + ImageBytes); // file size
        PutU32(Header + 10, 54);             // pixel data offset
        PutU32(Header + 14, 40);             // BITMAPINFOHEADER
        PutU32(Header + 18, Frame.Width);
        PutU32(Header + 22, Frame.Height);   // positive: bottom-up
        PutU16(Header + 26, 1);              // planes
        PutU16(Header + 28, 24);             // bits per pixel
        PutU32(Header + 34, ImageBytes);
        std::FILE* const File = std::fopen(FileName, "wb");
        if (File == nullptr)
            return false;
        bool bOk = std::fwrite(Header, sizeof(Header), 1, File) == 1;
        std::vector<unsigned char> Row(RowBytes, 0);
        for (std::uint32_t Y = Frame.Height; bOk && Y-- > 0;)
        {
            const std::uint8_t* const Source = Frame.Bgra.data() + static_cast<std::size_t>(Y) * Frame.Width * 4;
            for (std::uint32_t X = 0; X < Frame.Width; ++X)
            {
                Row[X * 3 + 0] = Source[X * 4 + 0];
                Row[X * 3 + 1] = Source[X * 4 + 1];
                Row[X * 3 + 2] = Source[X * 4 + 2];
            }
            bOk = std::fwrite(Row.data(), RowBytes, 1, File) == 1;
        }
        return std::fclose(File) == 0 && bOk;
    }
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
    Identity(World_);
    Identity(View_);
}

FRpGpu::~FRpGpu()
{
    Backend_.reset();
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

IRpGpuBackend& FRpGpu::Backend()
{
    if (Backend_ == nullptr)
    {
        Backend_ = bNullDevice_ ? CreateRpGpuNullBackend() : CreateRpGpuSdlBackend();
        bBackendIsNull_ = bNullDevice_;
    }
    return *Backend_;
}

void FRpGpu::LoadModes()
{
    if (bModesLoaded_)
        return;
    std::vector<FRpGpuDisplayMode> Found;
    if (!Backend().QueryDisplayModes(&Found))
        return; // asked again next time
    bModesLoaded_ = true;
    Modes_.clear();
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
    ReadSettings();
    // A changed NULL_DEVICE replaces the backend; the client's next CreateBuffers brings the new
    // one up.
    if (Backend_ != nullptr && bBackendIsNull_ != bNullDevice_)
    {
        Backend_.reset();
        bBuffersActive_ = false;
        bModesLoaded_ = false;
        Modes_.clear(); // a failed query on the new backend must not report the old one's modes
    }
    ResetStateToDefaults();
    bStarted_ = true;
    return GR_OK;
}

// RP8's shutdown ends the buffer session first (DestroyGpuResources), then releases the device.
GENRESULT DACOM_API FRpGpu::ShutdownPipeline()
{
    DestroyGpuResources();
    if (Backend_ != nullptr)
        Backend_->Destroy();
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
    // An ability key in the ini section replaces the device's answer, as in RP8.
    if (bAbilityOverridden_[Index])
        Value = AbilityOverrides_[Index];
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
GENRESULT DACOM_API FRpGpu::CreateBuffers(RpWindow Hwnd, int* Mode, int* OutMode)
{
    if (!bStarted_ || Mode == nullptr)
        return GR_GENERIC;
    const unsigned char* const ModeBytes = reinterpret_cast<const unsigned char*>(Mode);
    const int Interval = Mode[7];
    bBuffersActive_ = false;
    if (!Backend().Create(Hwnd, static_cast<std::uint32_t>(std::max(0, Mode[2])),
                          static_cast<std::uint32_t>(std::max(0, Mode[3])), Interval != 0))
    {
        Log("RPGPU: no device for the window: %s\n", Backend().LastError());
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
    ApplyIdentityTransforms();
    ResetLights();
    ResetStreams();
    if (Viewport_[2] == 0)
    {
        Viewport_[2] = Mode[2];
        Viewport_[3] = Mode[3];
    }
    bBuffersActive_ = true;
    // A new device has no GPU copies: every texture goes up again when it is next drawn.
    for (FRpGpuTexture* Texture : Textures_)
        Texture->bGpuStale = true;
    Frame_.Clear();
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

// RP8 releases the device's buffers here and keeps its handles; the next CreateBuffers starts a
// new session. RPGPU's buffers and textures live in CPU memory and survive as they are.
GENRESULT DACOM_API FRpGpu::DestroyGpuResources()
{
    bBuffersActive_ = false;
    return GR_OK;
}

// Flags: 1 colour (pipeline state 8), 2 depth (9, a float's bits), 4 stencil (10). Recorded in
// the frame, in order with the draws. As Direct3D 8's Clear, it covers the viewport, or Rects
// (RP8 passes one D3DRECT: left, top, right, bottom) cut to the viewport. A clear of the whole
// target is a render-pass clear; any other is drawn as a quad that writes the clear colour and
// depth over its pixels, with no depth test, blending or write mask. Stencil is not kept.
GENRESULT DACOM_API FRpGpu::ClearBuffers(unsigned long Flags, const void* Rects)
{
    if (!bBuffersActive_ || (Flags & 3) == 0)
        return GR_OK;
    const std::uint32_t Argb = static_cast<std::uint32_t>(PipelineState_[kPsClearColor]);
    float Depth = 1.0f;
    std::memcpy(&Depth, &PipelineState_[kPsClearZDepth], sizeof(float));

    const int TargetW = static_cast<int>(Backend().Width());
    const int TargetH = static_cast<int>(Backend().Height());
    const bool bWholeViewport = Viewport_[0] <= 0 && Viewport_[1] <= 0 &&
                                (Viewport_[2] <= 0 || Viewport_[0] + Viewport_[2] >= TargetW) &&
                                (Viewport_[3] <= 0 || Viewport_[1] + Viewport_[3] >= TargetH);
    if (Rects == nullptr && bWholeViewport)
    {
        // Only a whole-target colour clear sets the colour a frame without a leading clear
        // starts from; a partial clear must not reach the pixels outside its area.
        if ((Flags & 1) != 0)
            Backend().SetClearColor(Argb);
        FRpGpuCommand Clear;
        Clear.Kind = FRpGpuCommand::EKind::Clear;
        Clear.bClearColor = (Flags & 1) != 0;
        Clear.bClearDepth = (Flags & 2) != 0;
        Clear.ClearArgb = Argb;
        Clear.ClearDepth = Depth;
        Frame_.Commands.push_back(Clear);
        return GR_OK;
    }

    // The area: the viewport (the target when it has no size yet), cut to the rectangle.
    long Left = Viewport_[0];
    long Top = Viewport_[1];
    long Right = Viewport_[2] > 0 ? static_cast<long>(Viewport_[0]) + Viewport_[2] : TargetW;
    long Bottom = Viewport_[3] > 0 ? static_cast<long>(Viewport_[1]) + Viewport_[3] : TargetH;
    if (Rects != nullptr)
    {
        std::int32_t Raw[4]; // D3DRECT: four LONGs, 32-bit on Win32 and x64 (LLP64)
        std::memcpy(Raw, Rects, sizeof(Raw));
        Left = std::max<long>(Left, Raw[0]);
        Top = std::max<long>(Top, Raw[1]);
        Right = std::min<long>(Right, Raw[2]);
        Bottom = std::min<long>(Bottom, Raw[3]);
    }
    if (Left >= Right || Top >= Bottom)
        return GR_OK; // nothing left to clear

    const std::uint32_t FirstVertex = static_cast<std::uint32_t>(Frame_.Vertices.size());
    const float X[2] = {static_cast<float>(Left), static_cast<float>(Right)};
    const float Y[2] = {static_cast<float>(Top), static_cast<float>(Bottom)};
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        FRpGpuVertex Vertex = {};
        Vertex.X = X[Corner & 1];
        Vertex.Y = Y[Corner >> 1];
        Vertex.Z = Depth;
        Vertex.W = 1.0f;
        Vertex.Diffuse = Argb;
        Frame_.Vertices.push_back(Vertex);
    }
    FRpGpuCommand Quad;
    Quad.Kind = FRpGpuCommand::EKind::Draw;
    Quad.Pipeline.Topology = ERpGpuTopology::TriangleStrip;
    Quad.Pipeline.bZEnable = 1;
    Quad.Pipeline.bZWrite = (Flags & 2) != 0 ? 1 : 0;
    Quad.Pipeline.ZFunc = 8; // ALWAYS
    Quad.Pipeline.bBlend = 0;
    Quad.Pipeline.ColorWriteMask = (Flags & 1) != 0 ? 0xF : 0;
    Quad.Pipeline.CullMode = 1;
    Quad.Pipeline.FillMode = 3;
    Quad.FirstVertex = FirstVertex;
    Quad.VertexCount = 4;
    std::memcpy(Quad.Viewport, Viewport_, sizeof(Quad.Viewport));
    Quad.MinZ = 0.0f;
    Quad.MaxZ = 1.0f;
    Quad.Transform.Mode[1] = 1.0f; // pre-transformed: pixels, z as given
    Quad.Transform.Flags[1] = 2;   // the diffuse colour is the vertex's
    for (int Stage = 0; Stage < kRpGpuStages; ++Stage)
        Quad.Stage.Color[Stage][0] = 1; // every stage off: the diffuse colour passes through
    Frame_.Commands.push_back(Quad);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SwapBuffers()
{
    // A failure sends the client through its buffer recovery (CreateBuffers again), which
    // rebuilds the device.
    if (!bBuffersActive_ || !Backend().IsReady())
        return GR_GENERIC;
    const bool bPresented = Backend().Present(Frame_);
    Frame_.Clear();
    // The frame is submitted: the image versions it was the last to use can go.
    for (std::uint64_t Version : RetiredVersions_)
        Backend().ForgetTexture(Version);
    RetiredVersions_.clear();
    if (!bPresented)
    {
        Log("RPGPU: present failed: %s\n", Backend().LastError());
        return GR_GENERIC;
    }
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

GENRESULT DACOM_API FRpGpu::SetWindow(RpWindow Hwnd, int X, int Y, int W, int H)
{
    (void)Hwnd;
    (void)X;
    (void)Y;
    (void)W;
    (void)H;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetWindow(RpWindow* OutHwnd, int* OutLeft, int* OutRight, int* OutW, int* OutH)
{
    if (OutHwnd != nullptr)
        *OutHwnd = Backend().Hwnd();
    if (OutLeft != nullptr)
        *OutLeft = 0;
    if (OutRight != nullptr)
        *OutRight = 0;
    if (OutW != nullptr)
        *OutW = static_cast<int>(Backend().Width());
    if (OutH != nullptr)
        *OutH = static_cast<int>(Backend().Height());
    return GR_OK;
}

// --- frame and state -------------------------------------------------------------------------

GENRESULT DACOM_API FRpGpu::BeginScene()
{
    return (bBuffersActive_ && Backend().IsReady()) ? GR_OK : GR_GENERIC;
}

GENRESULT DACOM_API FRpGpu::EndScene()
{
    return GR_OK;
}

// The draw calls record into the frame (RpGpuDraw.cpp); the return values stay RP8's: the
// game only propagates them. The stream selection (SelectedVb_, SelectedIb_) changes as RP8's
// stream cache does: the user-pointer draws reset it, the vertex-buffer draws select their
// buffers, and IRPDraw::DrawIndexedPrimitive draws from it. Every draw that passes RP8's
// primitive-count check runs RP8's FlushDrawState (a changed view is committed, queued lights
// are sent), whether or not RPGPU can record it.
GENRESULT DACOM_API FRpGpu::DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                          unsigned int Count, void* Unused)
{
    (void)Unused;
    if (!bBuffersActive_ || static_cast<int>(Count) <= 0)
        return GR_OK;
    FlushDrawState();
    RecordDraw(Prim, Fvf, static_cast<const unsigned char*>(Verts), Rp8FvfStride(Fvf), Count, nullptr, 0);
    ResetStreams();
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                                   unsigned int VertCount, const void* Indices,
                                                   unsigned int IndexCount, unsigned long Unused)
{
    (void)Unused;
    if (!bBuffersActive_ || PrimCountFromVerts(Prim, IndexCount) <= 0)
        return GR_OK;
    FlushDrawState();
    RecordDraw(Prim, Fvf, static_cast<const unsigned char*>(Verts), Rp8FvfStride(Fvf), VertCount,
               static_cast<const std::uint16_t*>(Indices), IndexCount);
    ResetStreams();
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex,
                                            unsigned int Count, void* Unused)
{
    (void)Unused;
    if (!bBuffersActive_ || PrimCountFromVerts(Prim, Count) <= 0)
        return GR_OK;
    SelectedVb_ = Vb;
    FlushDrawState();
    const FRpGpuBuffer* const Buffer = LiveVb(Vb);
    if (Buffer != nullptr && StartVertex <= Buffer->Count && Count <= Buffer->Count - StartVertex)
        RecordDraw(Prim, Buffer->Fvf, Buffer->Data.data() + static_cast<std::size_t>(StartVertex) * Buffer->Stride,
                   Buffer->Stride, Count, nullptr, 0);
    return GR_OK;
}

// RP8 copies the indices into its scratch index buffer and draws from there with BaseVertex as
// the base vertex, which leaves the vertex buffer and the scratch buffer selected. NumVerts is
// Direct3D's hint; the vertices the indices reach are what is drawn.
GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb, unsigned int BaseVertex,
                                                   unsigned int NumVerts, const void* Indices,
                                                   unsigned int IndexCount, unsigned long Unused)
{
    (void)Unused;
    (void)NumVerts;
    if (!bBuffersActive_ || PrimCountFromVerts(Prim, IndexCount) <= 0 || Indices == nullptr)
        return GR_OK;
    // More indices than the scratch ring holds fail, as RP8's LockRange fails them.
    unsigned int Start = 0;
    if (!LockRing(&ScratchIb_, IndexCount, &Start))
        return kFail;
    std::memcpy(ScratchIb_.Data.data() + static_cast<std::size_t>(Start) * 2, Indices,
                static_cast<std::size_t>(IndexCount) * 2);
    SelectedVb_ = Vb;
    SelectedIb_ = kMinusOne;
    IbBaseVertex_ = BaseVertex;
    FlushDrawState();
    const FRpGpuBuffer* const Buffer = LiveVb(Vb);
    if (Buffer != nullptr && BaseVertex < Buffer->Count)
        RecordDraw(Prim, Buffer->Fvf, Buffer->Data.data() + static_cast<std::size_t>(BaseVertex) * Buffer->Stride,
                   Buffer->Stride, Buffer->Count - BaseVertex,
                   reinterpret_cast<const std::uint16_t*>(ScratchIb_.Data.data()) + Start, IndexCount);
    return GR_OK;
}

// Direct3D 8's DrawIndexedPrimitive on the selected buffers: Count indices from StartIndex, each
// counted from SelectIb's base vertex. MinIndex and NumVertices are Direct3D's hints.
GENRESULT DACOM_API FRpGpu::DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex, unsigned int NumVertices,
                                                 unsigned int StartIndex, unsigned int Count)
{
    (void)MinIndex;
    (void)NumVertices;
    if (!bBuffersActive_)
        return GR_GENERIC;
    if (PrimCountFromVerts(Prim, Count) <= 0)
        return kFail;
    FlushDrawState();
    const FRpGpuBuffer* const Vertices = LiveVb(SelectedVb_);
    const FRpGpuBuffer* const IndexBuffer = SelectedIb_ == kMinusOne ? &ScratchIb_ : LiveIb(SelectedIb_);
    if (Vertices == nullptr || IndexBuffer == nullptr || IbBaseVertex_ >= Vertices->Count ||
        StartIndex > IndexBuffer->Count || Count > IndexBuffer->Count - StartIndex)
        return kFail;
    RecordDraw(Prim, Vertices->Fvf,
               Vertices->Data.data() + static_cast<std::size_t>(IbBaseVertex_) * Vertices->Stride, Vertices->Stride,
               Vertices->Count - IbBaseVertex_,
               reinterpret_cast<const std::uint16_t*>(IndexBuffer->Data.data()) + StartIndex, Count);
    return GR_OK;
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

// RP8's checks and answers (FUN_06d13d70): buffers must be up, a parent file system and a
// missing name are invalid parameters, the extension picks the format. RP8 saves through D3DX in
// seven formats; RPGPU writes BMP (.BMP, .DIB) itself and fails the others. The image is the
// backend's last presented frame; a backend that cannot read frames back fails.
GENRESULT DACOM_API FRpGpu::PrintScreen(void* ParentFileSystem, const char* FileName)
{
    if (!bBuffersActive_ || !Backend().IsReady())
        return GR_GENERIC;
    if (ParentFileSystem != nullptr || FileName == nullptr)
        return GR_INVALID_PARMS;
    const char* const Extension = std::strrchr(FileName, '.');
    if (Extension == nullptr || (_stricmp(Extension, ".BMP") != 0 && _stricmp(Extension, ".DIB") != 0))
    {
        Log("RPGPU: print_screen: no BMP extension on %s\n", FileName);
        return GR_GENERIC;
    }
    FRpGpuFrame Frame;
    if (!Backend().ReadBack(&Frame))
    {
        Log("RPGPU: print_screen: the backend cannot read the frame back\n");
        return GR_GENERIC;
    }
    return WriteBmp(FileName, Frame) ? GR_OK : GR_GENERIC;
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
