// PORT(rpgpu): RPGPU.dll, the SDL3 + SDL_GPU render pipeline; no retail counterpart.

// RPGPU's draw recording (docs/re/rpgpu.md, "Drawing"): every draw call converts the game's
// vertices into FRpGpuVertex and records one command with the render state and transforms of
// the moment; SwapBuffers hands the frame to the backend. Pre-transformed vertices
// (D3DFVF_XYZRHW: the menus, the HUD) are drawn in pixels, the others through world * view *
// projection, lit and fogged as Direct3D 8's fixed-function stages do (RpGpuLighting.cpp), with
// up to kRpGpuStages texture stages.

#include "RpGpu.h"

#include "RpGpuCommon.h"
#include "render/rp8_semantics.h"
#include "TexConvert.h"

#include <algorithm>
#include <cstring>
#include <new>

using namespace RpGpuDetail;

namespace
{
    // Direct3D 8 render-state and texture-stage-state numbers this file reads.
    enum : int
    {
        kRsZEnable = 7,
        kRsFillMode = 8,
        kRsZWriteEnable = 14,
        kRsAlphaTestEnable = 15,
        kRsSrcBlend = 19,
        kRsDestBlend = 20,
        kRsCullMode = 22,
        kRsZFunc = 23,
        kRsAlphaRef = 24,
        kRsAlphaFunc = 25,
        kRsAlphaBlendEnable = 27,
        kRsZBias = 47,
        kRsColorWriteEnable = 0xA8,

        kRsTextureFactor = 60,
        kRsVertexBlend = 151,
        kRsIndexedVertexBlendEnable = 167,

        kTssColorOp = 1,
        kTssColorArg1 = 2,
        kTssColorArg2 = 3,
        kTssAlphaOp = 4,
        kTssAlphaArg1 = 5,
        kTssAlphaArg2 = 6,
        kTssTexCoordIndex = 11,
        kTssAddressU = 13,
        kTssAddressV = 14,
        kTssMagFilter = 16,
        kTssMinFilter = 17,
        kTssMipFilter = 18,
        kTssMipMapLodBias = 19,
        kTssMaxMipLevel = 20,
        kTssMaxAnisotropy = 21,
        kTssTextureTransformFlags = 24,
        kTssColorArg0 = 26,
        kTssAlphaArg0 = 27,
        kTssResultArg = 28,
    };

    constexpr unsigned int kFvfPositionMask = 0xE;
    constexpr unsigned int kFvfXyzRhw = 0x4;
    constexpr unsigned int kFvfNormal = 0x10;
    constexpr unsigned int kFvfPSize = 0x20;
    constexpr unsigned int kFvfDiffuse = 0x40;
    constexpr unsigned int kFvfSpecular = 0x80;
    constexpr unsigned int kFvfLastBetaUByte4 = 0x1000;

    float ReadFloat(const unsigned char* P)
    {
        float Value;
        std::memcpy(&Value, P, sizeof(Value));
        return Value;
    }

    std::uint32_t ReadU32(const unsigned char* P)
    {
        std::uint32_t Value;
        std::memcpy(&Value, P, sizeof(Value));
        return Value;
    }

    // Where a vertex of an FVF keeps what RPGPU reads, in bytes from its start (-1: absent).
    struct FLayout
    {
        bool bPretransformed = false;
        int Weights = 0;  // blend weights after the position (XYZB1..XYZB5)
        int Indices = -1; // the last beta as four matrix-index bytes (D3DFVF_LASTBETA_UBYTE4)
        int Normal = -1;
        int Diffuse = -1;
        int Specular = -1;
        int TexCount = 0;
        int Tex[8] = {};
        int TexFloats[8] = {};
        int Size = 0; // the bytes all of it takes; at most the stride
    };

    // Direct3D 8's vertex layout: the position (XYZ 12 bytes, XYZRHW 16, XYZB1..XYZB5 12 plus 4
    // per blend weight), then normal, point size, diffuse, specular and the texture sets.
    FLayout LayoutOf(unsigned int Fvf)
    {
        FLayout Layout;
        int Offset = 0;
        switch (Fvf & kFvfPositionMask)
        {
        case 0x2: Offset = 12; break;
        case kFvfXyzRhw: Offset = 16; Layout.bPretransformed = true; break;
        case 0x6: Offset = 16; break;
        case 0x8: Offset = 20; break;
        case 0xA: Offset = 24; break;
        case 0xC: Offset = 28; break;
        case 0xE: Offset = 32; break;
        default: break;
        }
        // XYZB1..XYZB5 carry 1..5 betas after x, y, z; with LASTBETA_UBYTE4 the last is indices.
        const unsigned int Position = Fvf & kFvfPositionMask;
        if (Position >= 0x6)
        {
            const int Betas = static_cast<int>((Position - 0x6) / 2 + 1);
            Layout.Weights = Betas;
            if ((Fvf & kFvfLastBetaUByte4) != 0)
            {
                Layout.Weights = Betas - 1;
                Layout.Indices = 12 + (Betas - 1) * 4;
            }
        }
        if ((Fvf & kFvfNormal) != 0)
        {
            Layout.Normal = Offset;
            Offset += 12;
        }
        if ((Fvf & kFvfPSize) != 0)
            Offset += 4;
        if ((Fvf & kFvfDiffuse) != 0)
        {
            Layout.Diffuse = Offset;
            Offset += 4;
        }
        if ((Fvf & kFvfSpecular) != 0)
        {
            Layout.Specular = Offset;
            Offset += 4;
        }
        Layout.TexCount = static_cast<int>((Fvf >> 8) & 0xF);
        if (Layout.TexCount > 8)
            Layout.TexCount = 8;
        for (int Set = 0; Set < Layout.TexCount; ++Set)
        {
            const unsigned int Size = (Fvf >> (16 + Set * 2)) & 3; // D3DFVF_TEXCOORDSIZEn
            Layout.Tex[Set] = Offset;
            Layout.TexFloats[Set] = Size == 0 ? 2 : Size == 1 ? 3 : Size == 2 ? 4 : 1;
            Offset += Layout.TexFloats[Set] * 4;
        }
        Layout.Size = Offset;
        return Layout;
    }

    // RP8 pixel formats that go to the GPU as block-compressed images.
    bool BlockFormat(unsigned int Format, ERpGpuTextureFormat* Out)
    {
        switch (Format)
        {
        case TexConvert::kDxt1: *Out = ERpGpuTextureFormat::Bc1; return true;
        case TexConvert::kDxt2:
        case TexConvert::kDxt3: *Out = ERpGpuTextureFormat::Bc2; return true;
        case TexConvert::kDxt4:
        case TexConvert::kDxt5: *Out = ERpGpuTextureFormat::Bc3; return true;
        default: return false;
        }
    }

    // One of the game's vertices in FRpGpuVertex form. TexOffset / TexFloats: where each stage's
    // texture-coordinate set is in the vertex and how many floats it has (-1 / 0: none).
    FRpGpuVertex ConvertVertex(const unsigned char* P, const FLayout& Layout, const int* TexOffset,
                               const int* TexFloats)
    {
        FRpGpuVertex Vertex;
        Vertex.X = ReadFloat(P);
        Vertex.Y = ReadFloat(P + 4);
        Vertex.Z = ReadFloat(P + 8);
        Vertex.W = Layout.bPretransformed ? ReadFloat(P + 12) : 1.0f;
        Vertex.Diffuse = Layout.Diffuse >= 0 ? ReadU32(P + Layout.Diffuse) : 0xFFFFFFFFu;
        for (int Weight = 0; Weight < 3; ++Weight)
            Vertex.Weights[Weight] = Weight < Layout.Weights ? ReadFloat(P + 12 + Weight * 4) : 0.0f;
        Vertex.Indices = Layout.Indices >= 0 ? ReadU32(P + Layout.Indices) : 0x03020100u;
        for (int Stage = 0; Stage < kRpGpuStages; ++Stage)
        {
            Vertex.Tex[Stage][0] = TexFloats[Stage] >= 1 ? ReadFloat(P + TexOffset[Stage]) : 0.0f;
            Vertex.Tex[Stage][1] = TexFloats[Stage] >= 2 ? ReadFloat(P + TexOffset[Stage] + 4) : 0.0f;
        }
        Vertex.Nx = Layout.Normal >= 0 ? ReadFloat(P + Layout.Normal) : 0.0f;
        Vertex.Ny = Layout.Normal >= 0 ? ReadFloat(P + Layout.Normal + 4) : 0.0f;
        Vertex.Nz = Layout.Normal >= 0 ? ReadFloat(P + Layout.Normal + 8) : 0.0f;
        Vertex.Specular = Layout.Specular >= 0 ? ReadU32(P + Layout.Specular) : 0u;
        return Vertex;
    }

    void ColourOf(std::uint32_t Argb, float* Out)
    {
        Out[0] = static_cast<float>((Argb >> 16) & 0xFF) / 255.0f;
        Out[1] = static_cast<float>((Argb >> 8) & 0xFF) / 255.0f;
        Out[2] = static_cast<float>(Argb & 0xFF) / 255.0f;
        Out[3] = static_cast<float>((Argb >> 24) & 0xFF) / 255.0f;
    }

    void Multiply(float* Out, const float* A, const float* B)
    {
        float R[16];
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                R[Row * 4 + Col] = A[Row * 4] * B[Col] + A[Row * 4 + 1] * B[4 + Col] + A[Row * 4 + 2] * B[8 + Col] +
                                   A[Row * 4 + 3] * B[12 + Col];
            }
        }
        std::memcpy(Out, R, sizeof(R));
    }
}

// The whole texture, every level (of every face, for a cube map), as the backend takes it: DXT
// blocks as they are when the backend samples them and level 0 is a whole number of blocks
// (Direct3D 12 requires that), everything else converted to B, G, R, A bytes by TexConvert.
void FRpGpu::RefreshTexture(FRpGpuTexture* Texture)
{
    if (Texture == nullptr || !Texture->bGpuStale || Texture->Images.empty())
        return;
    const std::size_t Levels = Texture->Images.size();
    FRpGpuTextureImage Image;
    Image.Width = Texture->Images[0].Width;
    Image.Height = Texture->Images[0].Height;
    Image.bCube = Texture->bCube;
    try
    {
        Image.Levels.resize(Levels);
        if (BlockFormat(Texture->Format, &Image.Format) && Backend().SupportsBlockCompression() &&
            Image.Width % 4 == 0 && Image.Height % 4 == 0)
        {
            // RPGPU keeps DXT levels tightly packed (RpGpuResources.cpp, MakeImage).
            for (std::size_t Level = 0; Level < Levels; ++Level)
                Image.Levels[Level] = Texture->Images[Level].Bits;
        }
        else
        {
            Image.Format = ERpGpuTextureFormat::Bgra8;
            for (std::size_t Level = 0; Level < Levels; ++Level)
            {
                const FRpGpuImage& Source = Texture->Images[Level];
                std::vector<std::uint8_t>& Bgra = Image.Levels[Level];
                Bgra.resize(static_cast<std::size_t>(Source.Width) * Source.Height * 4);
                // TexConvert's A8R8G8B8 is B, G, R, A in memory.
                TexConvert::FImage To;
                To.Format = TexConvert::kA8R8G8B8;
                To.Width = Source.Width;
                To.Height = Source.Height;
                To.Pitch = Source.Width * 4;
                To.Bits = Bgra.data();
                TexConvert::FSource From;
                From.Format = Texture->Format;
                From.Width = Source.Width;
                From.Height = Source.Height;
                From.Pitch = Source.Pitch;
                From.Bits = Source.Bits.data();
                if (TexConvert::Load(To, From) == TexConvert::EResult::Failed)
                    return; // e.g. P8 with no palette kept: drawn untextured
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    // A new version: the old one may still be in this frame's earlier draws.
    if (Texture->GpuVersion != 0)
        RetiredVersions_.push_back(Texture->GpuVersion);
    Texture->GpuVersion = NextTextureVersion_++;
    Backend().UploadTexture(Texture->GpuVersion, std::move(Image));
    Texture->bGpuStale = false;
}

void FRpGpu::RetireBlocks(FRpGpuBuffer* Buffer)
{
    if (Buffer == nullptr)
        return;
    for (const FRpGpuBuffer::FGpuBlock& Block : Buffer->GpuBlocks)
    {
        try
        {
            RetiredBlocks_.push_back(Block.Key);
        }
        catch (const std::bad_alloc&)
        {
            // No room to wait for the frame's end: let it go now (a draw recorded with it this
            // frame is then skipped).
            Backend().ForgetBlock(Block.Key);
        }
    }
    Buffer->GpuBlocks.clear();
}

// A buffer that has stayed as it is: not a ring, not locked, not written in this frame
// (FRpGpuBuffer::LastWriteFrame). Only such buffers get GPU blocks.
bool FRpGpu::IsSettled(const FRpGpuBuffer* Buffer) const
{
    return Buffer != nullptr && !Buffer->bRing && !Buffer->bLocked && Buffer->LastWriteFrame != FrameNumber_;
}

// The whole index buffer, handed to the backend to keep until a write; lost blocks as for
// vertex blocks (VertexBlockFor).
std::uint64_t FRpGpu::IndexBlockFor(FRpGpuBuffer* Buffer)
{
    if (!Buffer->GpuBlocks.empty())
    {
        const std::uint64_t Key = Buffer->GpuBlocks.front().Key;
        if (!Backend().IsBlockLost(Key))
            return Key;
        Backend().ForgetBlock(Key);
        Buffer->GpuBlocks.clear();
        Buffer->LastWriteFrame = FrameNumber_;
        return 0;
    }
    if (Buffer->Count == 0 || Buffer->Data.size() < static_cast<std::size_t>(Buffer->Count) * 2)
        return 0;
    std::vector<std::uint16_t> Indices;
    try
    {
        Indices.resize(Buffer->Count);
        std::memcpy(Indices.data(), Buffer->Data.data(), static_cast<std::size_t>(Buffer->Count) * 2);
        Buffer->GpuBlocks.reserve(1);
    }
    catch (const std::bad_alloc&)
    {
        return 0;
    }
    const std::uint64_t Key = NextTextureVersion_++;
    if (!Backend().UploadIndexBlock(Key, std::move(Indices)))
        return 0;
    Buffer->GpuBlocks.push_back({0, Key});
    return Key;
}

// The whole buffer, converted once for this texture-coordinate signature and handed to the
// backend to keep. Later draws that read the buffer the same way reuse it until a write.
std::uint64_t FRpGpu::VertexBlockFor(FRpGpuBuffer* Buffer, std::uint32_t Signature, const int* TexOffset,
                                     const int* TexFloats)
{
    for (std::size_t Index = 0; Index < Buffer->GpuBlocks.size(); ++Index)
    {
        const FRpGpuBuffer::FGpuBlock Block = Buffer->GpuBlocks[Index];
        if (Block.Signature != Signature)
            continue;
        if (!Backend().IsBlockLost(Block.Key))
            return Block.Key;
        // The device could not make its buffer: forget it, draw from the frame this time, and
        // make the block again in a later frame.
        Backend().ForgetBlock(Block.Key);
        Buffer->GpuBlocks.erase(Buffer->GpuBlocks.begin() + static_cast<std::ptrdiff_t>(Index));
        Buffer->LastWriteFrame = FrameNumber_;
        return 0;
    }
    const FLayout Layout = LayoutOf(Buffer->Fvf);
    if (Buffer->Count == 0 || Buffer->Stride == 0 || static_cast<unsigned int>(Layout.Size) > Buffer->Stride)
        return 0;
    std::vector<FRpGpuVertex> Vertices;
    try
    {
        Vertices.reserve(Buffer->Count);
        for (unsigned int Index = 0; Index < Buffer->Count; ++Index)
            Vertices.push_back(ConvertVertex(Buffer->Data.data() + static_cast<std::size_t>(Index) * Buffer->Stride,
                                             Layout, TexOffset, TexFloats));
        Buffer->GpuBlocks.reserve(Buffer->GpuBlocks.size() + 1);
    }
    catch (const std::bad_alloc&)
    {
        return 0; // drawn from the frame's vertices instead
    }
    const std::uint64_t Key = NextTextureVersion_++;
    if (!Backend().UploadVertexBlock(Key, std::move(Vertices)))
        return 0; // not queued: drawn from the frame's vertices instead
    Buffer->GpuBlocks.push_back({Signature, Key}); // reserved above, so this cannot throw
    return Key;
}

void FRpGpu::RecordDraw(FlULong Prim, unsigned int Fvf, const unsigned char* Verts, unsigned int Stride,
                        unsigned int VertCount, const std::uint16_t* Indices, unsigned int IndexCount,
                        FRpGpuBuffer* StaticSource, unsigned int SourceBase, FRpGpuBuffer* IndexSource,
                        unsigned int IndexStart)
{
    if (!bBuffersActive_ || Verts == nullptr || Stride == 0 || VertCount == 0)
        return;
    if ((Fvf & kFvfPositionMask) == 0)
        return; // no position: nothing to draw, and nothing safe to read
    // RP8's stride (Rp8FvfStride) leaves out the point size, as RP8 does; a format with one is
    // longer than that stride and is not drawn, rather than read with the wrong layout.
    const FLayout Layout = LayoutOf(Fvf);
    if (static_cast<unsigned int>(Layout.Size) > Stride)
        return; // a stride too small for the format: nothing safe to read
    const unsigned int Count = Indices != nullptr ? IndexCount : VertCount;
    const int Primitives = PrimCountFromVerts(Prim, Count);
    if (Primitives <= 0)
        return;
    // The topology, and how many of the vertices or indices it uses.
    ERpGpuTopology Topology = ERpGpuTopology::Triangles;
    unsigned int Used = 0;
    bool bFan = false;
    switch (Prim)
    {
    case 2: Topology = ERpGpuTopology::Lines; Used = static_cast<unsigned int>(Primitives) * 2; break;
    case 3: Topology = ERpGpuTopology::LineStrip; Used = static_cast<unsigned int>(Primitives) + 1; break;
    case 4: Topology = ERpGpuTopology::Triangles; Used = static_cast<unsigned int>(Primitives) * 3; break;
    case 5: Topology = ERpGpuTopology::TriangleStrip; Used = static_cast<unsigned int>(Primitives) + 2; break;
    case 6: Topology = ERpGpuTopology::Triangles; Used = static_cast<unsigned int>(Primitives) + 2; bFan = true; break;
    default: return; // points: not drawn yet
    }
    if (Used > Count)
        return;

    // Bounds: with indices, the highest one must lie within the vertices given.
    unsigned int Lowest = 0;
    unsigned int Highest = 0;
    if (Indices != nullptr)
    {
        Lowest = 0xFFFFu;
        for (unsigned int Index = 0; Index < Used; ++Index)
        {
            Lowest = std::min<unsigned int>(Lowest, Indices[Index]);
            Highest = std::max<unsigned int>(Highest, Indices[Index]);
        }
        if (Highest >= VertCount)
            return; // an index past the vertices it was given: nothing safe to draw
    }
    else if (Used > VertCount || (bFan && Used > 0x10000u))
    {
        return; // more vertices than given; or a fan too long for the 16-bit list it becomes
    }

    // The texture-coordinate set each stage reads (TEXCOORDINDEX's low bits; its high bits, the
    // coordinate generation, are the vertex shader's). A set the format lacks reads (0, 0). The
    // sets, four bits a stage, are the signature of a static buffer's GPU block.
    int TexOffset[kRpGpuStages] = {};
    int TexFloats[kRpGpuStages] = {};
    std::uint32_t Signature = 0;
    for (int Stage = 0; Stage < kRpGpuStages; ++Stage)
    {
        const int Set = StageStates_[Stage][kTssTexCoordIndex] & 0xFFFF;
        TexOffset[Stage] = Set < Layout.TexCount ? Layout.Tex[Set] : -1;
        TexFloats[Stage] = Set < Layout.TexCount ? Layout.TexFloats[Set] : 0;
        Signature |= static_cast<std::uint32_t>(Set < Layout.TexCount ? Set : 0xF) << (Stage * 4);
    }

    // A static vertex buffer is drawn from its GPU block: the vertices stay where they are and
    // the draw starts at SourceBase in it. Everything else is copied into the frame: with
    // indices, only the range they reach, the indices rebased onto it.
    // Not while it is locked or in a frame it was written in (FRpGpuBuffer::LastWriteFrame).
    const std::uint64_t Block = IsSettled(StaticSource) ? VertexBlockFor(StaticSource, Signature, TexOffset, TexFloats) : 0;
    unsigned int First = 0;
    unsigned int Span = Used;
    std::uint32_t FirstVertex = 0;
    if (Block != 0)
    {
        FirstVertex = SourceBase;
    }
    else
    {
        if (Indices != nullptr)
        {
            First = Lowest;
            Span = Highest - Lowest + 1;
        }
        FirstVertex = static_cast<std::uint32_t>(Frame_.Vertices.size());
        Frame_.Vertices.reserve(Frame_.Vertices.size() + Span);
        for (unsigned int Index = 0; Index < Span; ++Index)
            Frame_.Vertices.push_back(
                ConvertVertex(Verts + static_cast<std::size_t>(First + Index) * Stride, Layout, TexOffset, TexFloats));
    }

    // Indices when the draw has them, or when a fan becomes a list (SDL_GPU has no fans). With
    // its vertices in a block, a draw from a settled index buffer reads that buffer's block too:
    // the indices are unchanged, so they are drawn where they are.
    std::uint32_t FirstIndex = static_cast<std::uint32_t>(Frame_.Indices.size());
    std::uint32_t IndexTotal = 0;
    const std::uint64_t IndexBlock =
        Block != 0 && !bFan && Indices != nullptr && IsSettled(IndexSource) ? IndexBlockFor(IndexSource) : 0;
    if (IndexBlock != 0)
    {
        FirstIndex = IndexStart;
        IndexTotal = Used;
    }
    auto Source = [&](unsigned int At) -> std::uint16_t {
        return static_cast<std::uint16_t>(Indices != nullptr ? Indices[At] - First : At);
    };
    if (bFan)
    {
        for (int Triangle = 0; Triangle < Primitives; ++Triangle)
        {
            Frame_.Indices.push_back(Source(0));
            Frame_.Indices.push_back(Source(static_cast<unsigned int>(Triangle) + 1));
            Frame_.Indices.push_back(Source(static_cast<unsigned int>(Triangle) + 2));
        }
        IndexTotal = static_cast<std::uint32_t>(Primitives) * 3;
    }
    else if (Indices != nullptr && IndexBlock == 0)
    {
        for (unsigned int Index = 0; Index < Used; ++Index)
            Frame_.Indices.push_back(Source(Index));
        IndexTotal = Used;
    }

    FRpGpuCommand Command;
    Command.Kind = FRpGpuCommand::EKind::Draw;
    Command.Pipeline.Topology = Topology;
    Command.Pipeline.bZEnable = RenderStates_[kRsZEnable] != 0;
    Command.Pipeline.bZWrite = RenderStates_[kRsZWriteEnable] != 0;
    Command.Pipeline.ZFunc = static_cast<std::uint8_t>(RenderStates_[kRsZFunc]);
    Command.Pipeline.bBlend = RenderStates_[kRsAlphaBlendEnable] != 0;
    Command.Pipeline.SrcBlend = static_cast<std::uint8_t>(RenderStates_[kRsSrcBlend]);
    Command.Pipeline.DstBlend = static_cast<std::uint8_t>(RenderStates_[kRsDestBlend]);
    Command.Pipeline.ColorWriteMask = static_cast<std::uint8_t>(RenderStates_[kRsColorWriteEnable] & 0xF);
    Command.Pipeline.CullMode = static_cast<std::uint8_t>(RenderStates_[kRsCullMode]);
    Command.Pipeline.FillMode = static_cast<std::uint8_t>(RenderStates_[kRsFillMode]);
    Command.Pipeline.ZBias = static_cast<std::uint8_t>(std::clamp(RenderStates_[kRsZBias], 0, 16));
    // The stages, up to the first whose COLOROP is DISABLE: each one's operations, sampler,
    // texture and texture-coordinate handling.
    bool bStagesEnded = false;
    for (int Stage = 0; Stage < kRpGpuStages; ++Stage)
    {
        const int* const States = StageStates_[Stage];
        bStagesEnded = bStagesEnded || States[kTssColorOp] == 1;
        Command.Stage.Color[Stage][0] = bStagesEnded ? 1u : static_cast<std::uint32_t>(States[kTssColorOp]);
        if (bStagesEnded)
            continue;
        Command.Stage.Color[Stage][1] = static_cast<std::uint32_t>(States[kTssColorArg1]);
        Command.Stage.Color[Stage][2] = static_cast<std::uint32_t>(States[kTssColorArg2]);
        Command.Stage.Color[Stage][3] = static_cast<std::uint32_t>(States[kTssColorArg0]);
        Command.Stage.Alpha[Stage][0] = static_cast<std::uint32_t>(States[kTssAlphaOp]);
        Command.Stage.Alpha[Stage][1] = static_cast<std::uint32_t>(States[kTssAlphaArg1]);
        Command.Stage.Alpha[Stage][2] = static_cast<std::uint32_t>(States[kTssAlphaArg2]);
        Command.Stage.Alpha[Stage][3] = static_cast<std::uint32_t>(States[kTssAlphaArg0]);
        Command.Stage.Misc[Stage][0] = static_cast<std::uint32_t>(States[kTssResultArg]);

        FRpGpuSamplerKey& Sampler = Command.Sampler[Stage];
        Sampler.AddressU = static_cast<std::uint8_t>(States[kTssAddressU]);
        Sampler.AddressV = static_cast<std::uint8_t>(States[kTssAddressV]);
        Sampler.MagFilter = static_cast<std::uint8_t>(States[kTssMagFilter]);
        Sampler.MinFilter = static_cast<std::uint8_t>(States[kTssMinFilter]);
        Sampler.MipFilter = static_cast<std::uint8_t>(States[kTssMipFilter]);
        Sampler.MaxAnisotropy = static_cast<std::uint8_t>(std::clamp(States[kTssMaxAnisotropy], 1, 16));
        Sampler.LodBias = static_cast<std::uint32_t>(States[kTssMipMapLodBias]);
        FRpGpuTexture* const Texture = LiveTexture(StageTextures_[Stage]);
        RefreshTexture(Texture);
        Command.Texture[Stage] = Texture != nullptr ? Texture->GpuVersion : 0;
        Command.Stage.Misc[Stage][1] = Texture != nullptr ? 1u : 0u;
        // MAXMIPLEVEL past the texture's last level samples that last level, as in Direct3D.
        const int LastLevel = Texture != nullptr ? static_cast<int>(Texture->Levels) - 1 : 0;
        Sampler.MaxMipLevel = static_cast<std::uint8_t>(std::clamp(States[kTssMaxMipLevel], 0, std::max(0, LastLevel)));

        // TEXTURETRANSFORMFLAGS: the count (1..4) of transformed components, 0x100 PROJECTED.
        // Pre-transformed vertices take no transform and no generation, as in Direct3D.
        const unsigned int TransformFlags = static_cast<unsigned int>(States[kTssTextureTransformFlags]);
        const unsigned int TransformCount = TransformFlags & 0xFF;
        if (!Layout.bPretransformed && TransformCount != 0)
        {
            Command.Transform.TexTransform[Stage] = std::min(TransformCount, 4u);
            std::memcpy(Command.Transform.TexMatrix[Stage], StageTransforms_[Stage], sizeof(StageTransforms_[Stage]));
            if ((TransformFlags & 0x100) != 0)
                Command.Stage.Misc[Stage][2] = std::min(TransformCount, 4u);
        }
        const unsigned int Generation = (static_cast<unsigned int>(States[kTssTexCoordIndex]) >> 16) & 0xF;
        const unsigned int Components = TexFloats[Stage] == 1 ? 1u : 2u;
        Command.Transform.TexGen[Stage] = (Layout.bPretransformed ? 0u : std::min(Generation, 3u)) | (Components << 4);
    }
    ColourOf(static_cast<std::uint32_t>(RenderStates_[kRsTextureFactor]), Command.Stage.TextureFactor);
    Command.VertexBlock = Block;
    Command.IndexBlock = IndexBlock;
    Command.FirstVertex = FirstVertex;
    Command.VertexCount = Span;
    Command.FirstIndex = FirstIndex;
    Command.IndexCount = IndexTotal;
    std::memcpy(Command.Viewport, Viewport_, sizeof(Command.Viewport));
    if (Layout.bPretransformed)
    {
        // Pre-transformed vertices skip Direct3D's viewport transform, depth included: their z
        // is drawn as it is, whatever SetDepthRange says.
        Command.MinZ = 0.0f;
        Command.MaxZ = 1.0f;
    }
    else
    {
        Command.MinZ = DepthRange_[0];
        Command.MaxZ = DepthRange_[1];
        float* const Wvp = Command.Transform.WorldViewProjection;
        Multiply(Wvp, World_, View_);
        Multiply(Wvp, Wvp, Projection_);
        Multiply(Command.Transform.ViewProjection, View_, Projection_);
        // Vertex blending (VERTEXBLEND 1..3 weights, or 256, 0WEIGHTS: each vertex on its first
        // indexed matrix): the world matrices go up as a block, shared with the draws before when
        // they are the same. Tweening (255) is not drawn.
        const int BlendWeights = RenderStates_[kRsVertexBlend];
        const bool bIndexed = RenderStates_[kRsIndexedVertexBlendEnable] != 0;
        const bool bZeroWeights = BlendWeights == 256 && bIndexed;
        if ((BlendWeights >= 1 && BlendWeights <= 3) || bZeroWeights)
        {
            Command.Transform.Blend[0] = bZeroWeights ? 0u : static_cast<std::uint32_t>(BlendWeights);
            Command.Transform.Blend[1] = bIndexed ? 1u : 0u;
            Command.Transform.Blend[2] = bZeroWeights ? 1u : 0u;
            FRpGpuBlendUniforms Block;
            for (int Matrix = 0; Matrix < kRpGpuWorldMatrices; ++Matrix)
            {
                const float* const M = Matrix == 0 ? World_ : WorldN_[Matrix];
                for (int Col = 0; Col < 3; ++Col)
                {
                    for (int Row = 0; Row < 4; ++Row)
                        Block.Columns[Matrix][Col][Row] = M[Row * 4 + Col];
                }
            }
            if (Frame_.Blend.empty() || std::memcmp(&Frame_.Blend.back(), &Block, sizeof(Block)) != 0)
                Frame_.Blend.push_back(Block);
            Command.Blend = static_cast<std::int32_t>(Frame_.Blend.size() - 1);
        }
    }
    FillLightingAndFog(&Command, Layout.bPretransformed,
                       (Layout.Normal >= 0 ? 1u : 0u) | (Layout.Diffuse >= 0 ? 2u : 0u) |
                           (Layout.Specular >= 0 ? 4u : 0u));
    Command.Stage.AlphaTest[0] = RenderStates_[kRsAlphaTestEnable] != 0 ? 1u : 0u;
    Command.Stage.AlphaTest[1] = static_cast<std::uint32_t>(RenderStates_[kRsAlphaFunc]);
    Command.Stage.AlphaTest[2] = static_cast<std::uint32_t>(RenderStates_[kRsAlphaRef]) & 0xFFu;
    Frame_.Commands.push_back(Command);
}
