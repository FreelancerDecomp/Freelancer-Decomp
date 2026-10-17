// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "render/IBigImage2.h"

// Minimal 2D UI draw helpers (pipeline slot 69 / 70). The big-image tile callback FUN_00404720
// is ScreenQuadDrawBigImageTile in screen_quad_state.h.
// FVF 0x1C4: XYZRHW + DIFFUSE + SPECULAR + TEX1 (stride 32).

void MenuSetup2DRenderState();
void MenuRestore3DRenderState();
void MenuSetupAdditive2DRenderState();

void MenuDrawSolidRect(int Left, int Top, int Right, int Bottom, unsigned long PackedArgb);

void MenuDrawTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                          unsigned long PackedArgb, float U0, float V0, float U1, float V1);

void MenuDrawAdditiveTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                                  unsigned long PackedArgb, float U0, float V0, float U1, float V1);

// front_button-style bezel: indexed XYZRHW rings (114/108/60/36/96 idx) + 128² body + caption.
// 12-idx frame+text stays as the caption pass. Hover uses additive outer ring.
void MenuDrawNeuroNetButtonOverlay(int Left, int Top, int Right, int Bottom, void* BodyHandle,
                                   float BodyU0, float BodyV0, float BodyU1, float BodyV1,
                                   unsigned long BodyTint, void* LabelHandle, int LabelLeft,
                                   int LabelTop, int LabelRight, int LabelBottom,
                                   unsigned long FrameArgb, unsigned long LabelArgb, float LabelU1,
                                   float LabelV1);

void MenuDrawNeuroNetCursor(int X, int Y);
