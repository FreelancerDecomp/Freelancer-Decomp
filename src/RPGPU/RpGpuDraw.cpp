// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's draw recording (docs/re/rpgpu.md, "Drawing"): every draw call converts the game's
// vertices into FRpGpuVertex2D and records one command with the render state of the moment;
// SwapBuffers hands the frame to the backend. So far only pre-transformed vertices
// (D3DFVF_XYZRHW: the menus, the HUD) are drawn; the 3D draws come with the transform stage.

#include "RpGpu.h"

#include "RpGpuCommon.h"
#include "render/rp8_semantics.h"
#include "TexConvert.h"

#include <cstring>
#include <new>

using namespace RpGpuDetail;

namespace
{
    // Direct3D 8 render-state and texture-stage-state numbers this file reads.
    enum : int
    {
        kRsZEnable = 7,
        kRsZWriteEnable = 14,
        kRsAlphaTestEnable = 15,
        kRsSrcBlend = 19,
        kRsDestBlend = 20,
        kRsZFunc = 23,
        kRsAlphaRef = 24,
        kRsAlphaFunc = 25,
        kRsAlphaBlendEnable = 27,
        kRsColorWriteEnable = 0xA8,

        kTssColorOp = 1,
        kTssAlphaOp = 4,
        kTssAddressU = 13,
        kTssAddressV = 14,
        kTssMagFilter = 16,
        kTssMinFilter = 17,
        kTssMipFilter = 18,
    };

    constexpr unsigned int kFvfPositionMask = 0xE;
    constexpr unsigned int kFvfXyzRhw = 0x4;
    constexpr unsigned int kFvfNormal = 0x10;
    constexpr unsigned int kFvfDiffuse = 0x40;
    constexpr unsigned int kFvfSpecular = 0x80;

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

    // Where an XYZRHW vertex keeps its diffuse colour and first texture coordinate.
    struct FLayout
    {
        int Diffuse = -1;
        int Tex0 = -1;
        int Tex0Floats = 0;
    };

    // RP8's PrimCountFromVerts: primitives from a vertex or index count, with RP8's signed
    // divisions, so a count too small stays <= 0.
    int PrimCountFromVerts(unsigned long Prim, unsigned int Count)
    {
        const int Signed = static_cast<int>(Count);
        switch (Prim)
        {
        case 1: return Signed;
        case 2: return Signed / 2;
        case 3: return Signed - 1;
        case 4: return Signed / 3;
        case 5:
        case 6: return Signed - 2;
        default: return 0;
        }
    }

    FLayout LayoutOf(unsigned int Fvf)
    {
        FLayout Layout;
        int Offset = 16; // x, y, z, rhw
        if ((Fvf & kFvfNormal) != 0)
            Offset += 12;
        if ((Fvf & kFvfDiffuse) != 0)
        {
            Layout.Diffuse = Offset;
            Offset += 4;
        }
        if ((Fvf & kFvfSpecular) != 0)
            Offset += 4;
        if (((Fvf >> 8) & 0xF) != 0)
        {
            const unsigned int Size = (Fvf >> 16) & 3; // D3DFVF_TEXCOORDSIZEn of set 0
            Layout.Tex0 = Offset;
            Layout.Tex0Floats = Size == 0 ? 2 : Size == 1 ? 3 : Size == 2 ? 4 : 1;
        }
        return Layout;
    }
}

void FRpGpu::RefreshTexture(FRpGpuTexture* Texture)
{
    if (Texture == nullptr || !Texture->bGpuStale || Texture->Images.empty())
        return;
    // Level 0 as B, G, R, A bytes: TexConvert's A8R8G8B8 is exactly that in memory.
    const FRpGpuImage& Image = Texture->Images[0];
    std::vector<std::uint8_t> Bgra(static_cast<std::size_t>(Image.Width) * Image.Height * 4);
    TexConvert::FImage Destination;
    Destination.Format = TexConvert::kA8R8G8B8;
    Destination.Width = Image.Width;
    Destination.Height = Image.Height;
    Destination.Pitch = Image.Width * 4;
    Destination.Bits = Bgra.data();
    TexConvert::FSource Source;
    Source.Format = Texture->Format;
    Source.Width = Image.Width;
    Source.Height = Image.Height;
    Source.Pitch = Image.Pitch;
    Source.Bits = Image.Bits.data();
    try
    {
        if (TexConvert::Load(Destination, Source) == TexConvert::EResult::Failed)
            return; // e.g. P8 with no palette kept: drawn untextured
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    // A new version: the old one may still be in this frame's earlier draws.
    if (Texture->GpuVersion != 0)
        RetiredVersions_.push_back(Texture->GpuVersion);
    Texture->GpuVersion = NextTextureVersion_++;
    Backend().UploadTexture(Texture->GpuVersion, Image.Width, Image.Height, Bgra.data());
    Texture->bGpuStale = false;
}

void FRpGpu::RecordDraw(unsigned long Prim, unsigned int Fvf, const unsigned char* Verts, unsigned int Stride,
                        unsigned int VertCount, const std::uint16_t* Indices, unsigned int IndexCount)
{
    if (!bBuffersActive_ || Verts == nullptr || Stride == 0 || VertCount == 0)
        return;
    if ((Fvf & kFvfPositionMask) != kFvfXyzRhw)
        return; // transformed vertices: drawn once the transform stage exists
    const unsigned int Count = Indices != nullptr ? IndexCount : VertCount;
    const int Primitives = PrimCountFromVerts(Prim, Count);
    if (Primitives <= 0 || VertCount > 0x10000u)
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
    if (Indices != nullptr)
    {
        for (unsigned int Index = 0; Index < Used; ++Index)
        {
            if (Indices[Index] >= VertCount)
                return; // an index past the vertices it was given: nothing safe to draw
        }
    }

    // The vertices, in the shared layout.
    const FLayout Layout = LayoutOf(Fvf);
    const std::uint32_t FirstVertex = static_cast<std::uint32_t>(Frame_.Vertices.size());
    Frame_.Vertices.reserve(Frame_.Vertices.size() + VertCount);
    for (unsigned int Index = 0; Index < VertCount; ++Index)
    {
        const unsigned char* const P = Verts + static_cast<std::size_t>(Index) * Stride;
        FRpGpuVertex2D Vertex;
        Vertex.X = ReadFloat(P);
        Vertex.Y = ReadFloat(P + 4);
        Vertex.Z = ReadFloat(P + 8);
        Vertex.Rhw = ReadFloat(P + 12);
        Vertex.Diffuse = Layout.Diffuse >= 0 ? ReadU32(P + Layout.Diffuse) : 0xFFFFFFFFu;
        Vertex.U = Layout.Tex0Floats >= 1 ? ReadFloat(P + Layout.Tex0) : 0.0f;
        Vertex.V = Layout.Tex0Floats >= 2 ? ReadFloat(P + Layout.Tex0 + 4) : 0.0f;
        Frame_.Vertices.push_back(Vertex);
    }

    // Indices when the draw has them, or when a fan becomes a list (SDL_GPU has no fans).
    const std::uint32_t FirstIndex = static_cast<std::uint32_t>(Frame_.Indices.size());
    std::uint32_t IndexTotal = 0;
    auto Source = [&](unsigned int At) -> std::uint16_t {
        return Indices != nullptr ? Indices[At] : static_cast<std::uint16_t>(At);
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
    else if (Indices != nullptr)
    {
        Frame_.Indices.insert(Frame_.Indices.end(), Indices, Indices + Used);
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
    const int* const Stage0 = StageStates_[0];
    Command.Sampler.AddressU = static_cast<std::uint8_t>(Stage0[kTssAddressU]);
    Command.Sampler.AddressV = static_cast<std::uint8_t>(Stage0[kTssAddressV]);
    Command.Sampler.MagFilter = static_cast<std::uint8_t>(Stage0[kTssMagFilter]);
    Command.Sampler.MinFilter = static_cast<std::uint8_t>(Stage0[kTssMinFilter]);
    Command.Sampler.MipFilter = static_cast<std::uint8_t>(Stage0[kTssMipFilter]);
    FRpGpuTexture* const Texture = LiveTexture(StageTextures_[0]);
    RefreshTexture(Texture);
    Command.Texture = Texture != nullptr ? Texture->GpuVersion : 0;
    Command.FirstVertex = FirstVertex;
    Command.VertexCount = VertCount;
    Command.FirstIndex = FirstIndex;
    Command.IndexCount = IndexTotal;
    std::memcpy(Command.Viewport, Viewport_, sizeof(Command.Viewport));
    Command.MinZ = DepthRange_[0];
    Command.MaxZ = DepthRange_[1];
    Command.Ops[0] = static_cast<std::uint32_t>(Stage0[kTssColorOp]);
    Command.Ops[1] = static_cast<std::uint32_t>(Stage0[kTssAlphaOp]);
    Command.Ops[2] = Texture != nullptr ? 1u : 0u;
    Command.AlphaTest[0] = RenderStates_[kRsAlphaTestEnable] != 0 ? 1u : 0u;
    Command.AlphaTest[1] = static_cast<std::uint32_t>(RenderStates_[kRsAlphaFunc]);
    Command.AlphaTest[2] = static_cast<std::uint32_t>(RenderStates_[kRsAlphaRef]) & 0xFFu;
    Frame_.Commands.push_back(Command);
}
