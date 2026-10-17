// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "menu_ui_draw.h"

#include "DALib.h"
#include "render/IRenderPipeline.h"
#include "screen_quad_state.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1
    static const unsigned long kFvfScreen = 0x1C4u;
    static const unsigned long kPrimTriangleStrip = 5;
    static const unsigned long kPrimTriangleList = 4;

    struct FScreenVertex
    {
        float X;
        float Y;
        float Z;
        float Rhw;
        unsigned long Diffuse;
        unsigned long Specular;
        float U;
        float V;
    };

    IRenderPipeline* GetPipeline()
    {
        if (DALib::Pipeline == nullptr)
            return nullptr;
        return DALib::Pipeline;
    }

    void PipelineDrawPrimitive(unsigned long Prim, unsigned long Fvf, void* Verts,
                               unsigned int VertexCount)
    {
        IRenderPipeline* Pipe = GetPipeline();
        if (Pipe == nullptr)
            return;

        Pipe->DrawPrimitive(Prim, static_cast<unsigned int>(Fvf), Verts, VertexCount, nullptr);
    }

    void PipelineDrawIndexed(unsigned long Prim, unsigned long Fvf, void* Verts,
                             unsigned int VertexCount, unsigned short* Indices,
                             unsigned int IndexCount)
    {
        IRenderPipeline* Pipe = GetPipeline();
        if (Pipe == nullptr)
            return;

        // Seven arguments: slot 70 pops 0x20, and both retail call sites in Freelancer.exe
        // (0x004217b6, 0x00421973) push a trailing dword this one used to leave off.
        Pipe->DrawIndexedPrimitiveUp(Prim, static_cast<unsigned int>(Fvf), Verts, VertexCount,
                                     Indices, IndexCount, 0);
    }

    void FillQuad(FScreenVertex* Out, float X0, float Y0, float X1, float Y1, unsigned long Color,
                  float U0, float V0, float U1, float V1)
    {
        Out[0].X = X0;
        Out[0].Y = Y0;
        Out[0].Z = 0.f;
        Out[0].Rhw = 1.f;
        Out[0].Diffuse = Color;
        Out[0].Specular = 0;
        Out[0].U = U0;
        Out[0].V = V0;

        Out[1].X = X1;
        Out[1].Y = Y0;
        Out[1].Z = 0.f;
        Out[1].Rhw = 1.f;
        Out[1].Diffuse = Color;
        Out[1].Specular = 0;
        Out[1].U = U1;
        Out[1].V = V0;

        Out[2].X = X0;
        Out[2].Y = Y1;
        Out[2].Z = 0.f;
        Out[2].Rhw = 1.f;
        Out[2].Diffuse = Color;
        Out[2].Specular = 0;
        Out[2].U = U0;
        Out[2].V = V1;

        Out[3].X = X1;
        Out[3].Y = Y1;
        Out[3].Z = 0.f;
        Out[3].Rhw = 1.f;
        Out[3].Diffuse = Color;
        Out[3].Specular = 0;
        Out[3].U = U1;
        Out[3].V = V1;
    }

    void PointOnRect(float HalfW, float HalfH, float T01, float* OutX, float* OutY)
    {
        float Width = HalfW * 2.f;
        float Height = HalfH * 2.f;
        if (Width < 1.f)
            Width = 1.f;
        if (Height < 1.f)
            Height = 1.f;
        const float Perimeter = 2.f * (Width + Height);
        float Dist = T01 * Perimeter;
        if (Dist < 0.f)
            Dist = 0.f;
        if (Dist > Perimeter)
            Dist = Perimeter;
        if (Dist <= Width)
        {
            *OutX = -HalfW + Dist;
            *OutY = -HalfH;
            return;
        }
        Dist -= Width;
        if (Dist <= Height)
        {
            *OutX = HalfW;
            *OutY = -HalfH + Dist;
            return;
        }
        Dist -= Height;
        if (Dist <= Width)
        {
            *OutX = HalfW - Dist;
            *OutY = HalfH;
            return;
        }
        Dist -= Width;
        *OutX = -HalfW;
        *OutY = HalfH - Dist;
    }

    void FillAnnulus(FScreenVertex* Verts, unsigned short* Indices, int Segs, float Cx, float Cy,
                     float OuterHalfW, float OuterHalfH, float InnerHalfW, float InnerHalfH,
                     unsigned long Color)
    {
        if (Segs < 3)
            Segs = 3;
        for (int Index = 0; Index < Segs; ++Index)
        {
            const float T = static_cast<float>(Index) / static_cast<float>(Segs);
            float Ox = 0.f;
            float Oy = 0.f;
            float Ix = 0.f;
            float Iy = 0.f;
            PointOnRect(OuterHalfW, OuterHalfH, T, &Ox, &Oy);
            PointOnRect(InnerHalfW, InnerHalfH, T, &Ix, &Iy);
            const int Base = Index * 2;
            Verts[Base].X = Cx + Ox;
            Verts[Base].Y = Cy + Oy;
            Verts[Base].Z = 0.f;
            Verts[Base].Rhw = 1.f;
            Verts[Base].Diffuse = Color;
            Verts[Base].Specular = 0;
            Verts[Base].U = 0.f;
            Verts[Base].V = 0.f;
            Verts[Base + 1].X = Cx + Ix;
            Verts[Base + 1].Y = Cy + Iy;
            Verts[Base + 1].Z = 0.f;
            Verts[Base + 1].Rhw = 1.f;
            Verts[Base + 1].Diffuse = Color;
            Verts[Base + 1].Specular = 0;
            Verts[Base + 1].U = 0.f;
            Verts[Base + 1].V = 0.f;
        }
        for (int Index = 0; Index < Segs; ++Index)
        {
            const int Next = (Index + 1) % Segs;
            const int I0 = Index * 2;
            const int I1 = Index * 2 + 1;
            const int I2 = Next * 2;
            const int I3 = Next * 2 + 1;
            const int Base = Index * 6;
            Indices[Base + 0] = static_cast<unsigned short>(I0);
            Indices[Base + 1] = static_cast<unsigned short>(I2);
            Indices[Base + 2] = static_cast<unsigned short>(I1);
            Indices[Base + 3] = static_cast<unsigned short>(I1);
            Indices[Base + 4] = static_cast<unsigned short>(I2);
            Indices[Base + 5] = static_cast<unsigned short>(I3);
        }
    }

    void DrawAnnulus(float Cx, float Cy, float OuterHalfW, float OuterHalfH, float InnerHalfW,
                     float InnerHalfH, unsigned long Color, int Segs)
    {
        IRenderPipeline* Pipe = GetPipeline();
        if (Pipe == nullptr || Segs < 3)
            return;
        FScreenVertex Verts[40];
        unsigned short Indices[120];
        if (Segs > 19)
            Segs = 19;
        FillAnnulus(Verts, Indices, Segs, Cx, Cy, OuterHalfW, OuterHalfH, InnerHalfW, InnerHalfH,
                    Color);
        Pipe->SetTextureStageTexture(0, nullptr);
        Pipe->SetTextureStageState(0, 1, 2);
        Pipe->SetTextureStageState(0, 2, 0);
        Pipe->SetTextureStageState(0, 4, 2);
        Pipe->SetTextureStageState(0, 5, 0);
        PipelineDrawIndexed(kPrimTriangleList, kFvfScreen, Verts, static_cast<unsigned>(Segs * 2),
                            Indices, static_cast<unsigned>(Segs * 6));
    }

    void DrawScreenQuad(float X0, float Y0, float X1, float Y1, unsigned long Color, float U0,
                        float V0, float U1, float V1, void* TextureHandle)
    {
        IRenderPipeline* Pipe = GetPipeline();
        if (Pipe == nullptr)
            return;

        if (TextureHandle != nullptr)
            Pipe->SetTextureStageTexture(0, TextureHandle);
        else
            Pipe->SetTextureStageTexture(0, nullptr);

        FScreenVertex Vertices[4];
        FillQuad(Vertices, X0, Y0, X1, Y1, Color, U0, V0, U1, V1);
        PipelineDrawPrimitive(kPrimTriangleStrip, kFvfScreen, Vertices, 4);
    }
}

void MenuSetup2DRenderState()
{
    // Retail FUN_00403ea0 state-apply half (cached). Geometry batch flush deferred.
    ApplyScreenQuadRenderStates(GetPipeline());
}

void MenuRestore3DRenderState()
{
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe == nullptr)
        return;

    ScreenQuadSetRenderState(Pipe, 0x1b, 0);
    ScreenQuadSetRenderState(Pipe, 0x8d, 0);
    Pipe->SetTextureStageTexture(0, nullptr);
    Pipe->SetTextureStageTexture(1, nullptr);
    ScreenQuadInvalidateStateCache();
}

void MenuSetupAdditive2DRenderState()
{
    MenuSetup2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe == nullptr)
        return;
    ScreenQuadSetRenderState(Pipe, 0x13, 5);
    ScreenQuadSetRenderState(Pipe, 0x14, 2);
}

void MenuDrawSolidRect(int Left, int Top, int Right, int Bottom, unsigned long PackedArgb)
{
    MenuSetup2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe != nullptr)
    {
        Pipe->SetTextureStageTexture(0, nullptr);
        Pipe->SetTextureStageState(0, 1, 2);
        Pipe->SetTextureStageState(0, 2, 0);
        Pipe->SetTextureStageState(0, 4, 2);
        Pipe->SetTextureStageState(0, 5, 0);
    }
    DrawScreenQuad(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right),
                   static_cast<float>(Bottom), PackedArgb, 0.f, 0.f, 1.f, 1.f, nullptr);
}

void MenuDrawTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                          unsigned long PackedArgb, float U0, float V0, float U1, float V1)
{
    if (TextureHandle == nullptr)
    {
        MenuDrawSolidRect(Left, Top, Right, Bottom, PackedArgb);
        return;
    }

    MenuSetup2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe != nullptr)
        Pipe->SetTextureStageState(0, 4, 2);
    DrawScreenQuad(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right),
                   static_cast<float>(Bottom), PackedArgb, U0, V0, U1, V1, TextureHandle);
}

void MenuDrawAdditiveTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                                  unsigned long PackedArgb, float U0, float V0, float U1, float V1)
{
    if (TextureHandle == nullptr)
        return;
    MenuSetupAdditive2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe != nullptr)
        Pipe->SetTextureStageState(0, 4, 2);
    DrawScreenQuad(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right),
                   static_cast<float>(Bottom), PackedArgb, U0, V0, U1, V1, TextureHandle);
    MenuSetup2DRenderState();
}

void MenuDrawNeuroNetButtonOverlay(int Left, int Top, int Right, int Bottom, void* BodyHandle,
                                   float BodyU0, float BodyV0, float BodyU1, float BodyV1,
                                   unsigned long BodyTint, void* LabelHandle, int LabelLeft,
                                   int LabelTop, int LabelRight, int LabelBottom,
                                   unsigned long FrameArgb, unsigned long LabelArgb, float LabelU1,
                                   float LabelV1)
{
    MenuSetup2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe == nullptr)
        return;

    const float X0 = static_cast<float>(Left);
    const float Y0 = static_cast<float>(Top);
    const float X1 = static_cast<float>(Right);
    const float Y1 = static_cast<float>(Bottom);
    const float Cx = (X0 + X1) * 0.5f;
    const float Cy = (Y0 + Y1) * 0.5f;
    float HalfW = (X1 - X0) * 0.5f;
    float HalfH = (Y1 - Y0) * 0.5f;
    if (HalfW < 8.f)
        HalfW = 8.f;
    if (HalfH < 4.f)
        HalfH = 4.f;

    const unsigned long Dark = 0xE0101820u;
    // front_button.cmp bezel: segs 19/18/10/6/16 → idx 114/108/60/36/96 (retail UI draws).
    DrawAnnulus(Cx, Cy, HalfW + 3.f, HalfH + 3.f, HalfW - 1.f, HalfH - 1.f, Dark, 19);
    DrawAnnulus(Cx, Cy, HalfW + 1.5f, HalfH + 1.5f, HalfW - 2.5f, HalfH - 2.5f, FrameArgb, 18);
    DrawAnnulus(Cx, Cy, HalfW - 2.f, HalfH - 2.f, HalfW - 5.f, HalfH - 5.f, Dark, 10);

    const float Inset = 6.f;
    if (BodyHandle != nullptr)
    {
        Pipe->SetTextureStageState(0, 1, 4);
        Pipe->SetTextureStageState(0, 2, 2);
        Pipe->SetTextureStageState(0, 4, 4);
        DrawScreenQuad(X0 + Inset, Y0 + Inset, X1 - Inset, Y1 - Inset, BodyTint, BodyU0, BodyV0,
                       BodyU1, BodyV1, BodyHandle);
    }
    else
    {
        Pipe->SetTextureStageTexture(0, nullptr);
        Pipe->SetTextureStageState(0, 1, 2);
        Pipe->SetTextureStageState(0, 2, 0);
        Pipe->SetTextureStageState(0, 4, 2);
        Pipe->SetTextureStageState(0, 5, 0);
        DrawScreenQuad(X0 + Inset, Y0 + Inset, X1 - Inset, Y1 - Inset, 0xC0101820u, 0.f, 0.f, 1.f,
                       1.f, nullptr);
    }

    DrawAnnulus(Cx, Cy, HalfW, HalfH, HalfW - 2.f, HalfH - 2.f, FrameArgb, 6);

    if (LabelHandle != nullptr && LabelRight > LabelLeft && LabelBottom > LabelTop)
    {
        FScreenVertex Vertices[4];
        unsigned short Indices[6] = {0, 1, 2, 1, 3, 2};
        FillQuad(Vertices, static_cast<float>(LabelLeft), static_cast<float>(LabelTop),
                 static_cast<float>(LabelRight), static_cast<float>(LabelBottom), LabelArgb, 0.f,
                 0.f, LabelU1, LabelV1);
        Pipe->SetTextureStageTexture(0, LabelHandle);
        Pipe->SetTextureStageState(0, 1, 4);
        Pipe->SetTextureStageState(0, 2, 2);
        Pipe->SetTextureStageState(0, 4, 4);
        Pipe->SetTextureStageState(0, 5, 2);
        PipelineDrawIndexed(kPrimTriangleList, kFvfScreen, Vertices, 4, Indices, 6);
    }

    const unsigned long Glow = (BodyTint == 0xFFFFFFFFu) ? 0x6687C3E0u : 0;
    if (Glow != 0)
    {
        MenuSetupAdditive2DRenderState();
        DrawAnnulus(Cx, Cy, HalfW + 6.f, HalfH + 6.f, HalfW + 1.f, HalfH + 1.f, Glow, 16);
        MenuSetup2DRenderState();
    }

    Pipe->SetTextureStageState(0, 1, 4);
    Pipe->SetTextureStageState(0, 2, 2);
    Pipe->SetTextureStageState(0, 4, 2);
    Pipe->SetTextureStageTexture(0, nullptr);
}

void MenuDrawNeuroNetCursor(int X, int Y)
{
    MenuSetup2DRenderState();
    IRenderPipeline* Pipe = GetPipeline();
    if (Pipe == nullptr)
        return;

    const float X0 = static_cast<float>(X);
    const float Y0 = static_cast<float>(Y);
    // NeuroNet pointer mesh (not the 6-vert marker): head + stem, 12 tris.
    FScreenVertex Verts[10];
    unsigned short Indices[36] = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 6,
                                  1, 7, 2, 3, 8, 4, 5, 9, 6, 7, 8, 2, 8, 3, 2,
                                  4, 8, 9, 4, 9, 5};
    const unsigned long Color = 0xFFE8F8FFu;
    const float Tips[10][2] = {
        {0.f, 0.f},    {22.f, 6.f},   {8.f, 8.f},    {7.f, 22.f},  {4.f, 10.f},
        {12.f, 5.f},   {6.f, 4.f},    {16.f, 14.f},  {14.f, 18.f}, {10.f, 12.f},
    };
    for (int Index = 0; Index < 10; ++Index)
    {
        Verts[Index].X = X0 + Tips[Index][0];
        Verts[Index].Y = Y0 + Tips[Index][1];
        Verts[Index].Z = 0.f;
        Verts[Index].Rhw = 1.f;
        Verts[Index].Diffuse = Color;
        Verts[Index].Specular = 0;
        Verts[Index].U = 0.f;
        Verts[Index].V = 0.f;
    }
    Pipe->SetTextureStageTexture(0, nullptr);
    Pipe->SetTextureStageState(0, 1, 2);
    Pipe->SetTextureStageState(0, 2, 0);
    Pipe->SetTextureStageState(0, 4, 2);
    Pipe->SetTextureStageState(0, 5, 0);
    PipelineDrawIndexed(kPrimTriangleList, kFvfScreen, Verts, 10, Indices, 36);
    MenuSetupAdditive2DRenderState();
    Verts[0].Diffuse = 0x6687C3E0u;
    PipelineDrawIndexed(kPrimTriangleList, kFvfScreen, Verts, 10, Indices, 36);
    MenuSetup2DRenderState();
}
