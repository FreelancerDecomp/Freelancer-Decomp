// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_mesh_load.h"
#include "space_camera.h"

// Win3D Engine-instance submit for UI widgets (ButtonControl / StaticControl mesh branch).
//
// Retail:
//   FUN_0059efa0  — thiscall on mesh holder; RS 0x17/0xe then Renderer Slot7
//   FUN_00434630  — OR flags 0x8200 (/+0x2000 if cull-char==0), rearrange, jmp FUN_00434310
//   FUN_00434310  — game_opt 0xb gate; save/restore RS; QI IFLAppearance; Slot8(flags)→
//                   IMaterial::SetParameterValue(0, {4,r,g,b,a}); Renderer Slot7; restore RS
//   FUN_005a0340  — build 3x4 (GetInstanceTransform / Layout+WorldPos) + Offset@+0xc0 → 5a1130
//   LAB_0055b660  — cursor unproject via DAT_0067dbf8 (LAB_0040f520 math) → vt+0x10 (5a0340)
//   FUN_005a1130  — InverseTransform(+0x3c) then slab tests vs ±half
//
// G_Win3DSceneCamera is retail DAT_0067dbf8 (static ctor LAB_0059d020 → FUN_0040f150).

// Retail DAT_0067dbf8 — Slot7 Arch and UI unproject camera.
extern FSpaceCamera G_Win3DSceneCamera;

// DAT_0067dd10 -- the 3DMathEngine FUN_0059efa0 (0x0059f005..0x0059f073) and WinButton::Update
// FUN_0059f360 (0x0059f46b..0x0059f47c) test and fill (FUN_005b73e0) on first use; a raw scan
// finds no other reference. Defined in win3d_engine_submit.cpp.
struct I3DMathEngine;
extern I3DMathEngine* G_Win3DObjectMath;

// FUN_0059da60 -- &DAT_0067dbf8 (G_Win3DSceneCamera, constructed at static init by LAB_0059d020).
FSpaceCamera* GetWin3DSceneCamera();

// FUN_0059efa0 — thiscall shape: Holder provides Inst at +0x04.
void SubmitWin3DEngineInstance(FClientMeshHolder* Holder, void* ArchAnchor, unsigned char bFrontCull,
                               float Scale, unsigned long Flags);

// FUN_00434310 (cdecl, 7 args) — tinted submit (RGBA bytes, not pre-scaled floats). The +0xc
// byte goes to IFLAppearance::GetOverrideMaterial (0x00434403), the +0x18 byte picks cull / lighting
// (0x00434381); both callers (FUN_00434630, FUN_00434670) pass 1 for the first.
void SubmitWin3DTintedEngineInstance(void* ArchAnchor, void* EngineInstance,
                                     unsigned char MaterialSelector, unsigned long Flags,
                                     unsigned char const ColorRgba[4],
                                     unsigned char bCullLightingByte, float Scale);

// FUN_00434630 — wraps tinted submit: Flags |= 0x8200; if !bFrontCull Flags |= 0x2000.
void SubmitWin3DTintedEngineInstanceWrapped(void* ArchAnchor, void* EngineInstance,
                                            unsigned char bFrontCull,
                                            unsigned char const ColorRgba[4],
                                            unsigned char bLightingEnable, float Scale,
                                            unsigned long Flags);

// FUN_00433f40 (cdecl, 8 args) -- the material-parameter submit: nothing for instance -1
// (returns 1); else the cull / lighting states saved (pipeline +0xf4) and set from
// bCullLightingByte (4 / off, else 8 / on), the IFLAppearance material 0 (Slot8(0)) given
// parameter 0 = ColorRgba / 255 (value type 4) and parameter 0xa = bParam10 (type 1),
// IRenderer2 +0x1c (Camera, Instance, 1.0f, Flags | 0x100 [| 0x2000 without bFrontCull]
// [| 0x30 with a scaled identity when Scale != 1], matrix, material), the two states restored.
// Returns the renderer's answer. Callers: FUN_00434670 (0x004346ab), 0x00548411 (not ported).
int SubmitWin3DMaterialEngineInstance(void* Camera, void* EngineInstance, unsigned char bFrontCull,
                                      unsigned char const ColorRgba[4],
                                      unsigned char bCullLightingByte, float Scale,
                                      unsigned char bParam10, unsigned long Flags);

// FUN_00434670 (cdecl, 6 args) -- the outlined wire: FUN_00433f40 with black {0,0,0,0},
// lighting byte 0 and parameter 0xa off, then FUN_00434310 with ColorRgba and
// Flags | 0x8100 (| 0x2000 without bFrontCull). Caller: StaticControl::Draw (0x00588c77).
void SubmitWin3DOutlinedEngineInstance(void* Camera, void* EngineInstance,
                                       unsigned char bFrontCull, unsigned char const ColorRgba[4],
                                       float Scale, unsigned long Flags);

// LAB_0055b660 / LAB_0040f520 — screen pixel → normalized world-space direction.
// Matches: (sx-cx)/XScale*ZNear, (sy-cy)/YScale*ZNear, -ZNear; TransformMatrix(World); normalize.
void Win3DUnprojectScreenRayDir(FSpaceCamera* Cam, float ScreenX, float ScreenY, float OutDir[3]);

// FUN_005a1130 — this = half extents; InverseTransform(matrix, rayDir as point) then
// LocalDir = Out+T, LocalOrigin = -T; three slab tests (strict < half). RET 8.
bool Win3DRayHitsAabbHalfExtents(float const HalfExtents[3], float const Matrix3x4[12],
                                 float const RayDir[3]);

