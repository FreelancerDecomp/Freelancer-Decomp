// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Marker and Thorn_MarkerReference (RTTI .?AUThorn_Marker@@ /
// .?AUThorn_MarkerReference@@) -- the objects MARKER entities are bound to.
//
// A Thorn_Marker (operator new(0x9c) in FUN_0044d810, ctor FUN_00453690, identity vtable
// 0x005cd8dc) is nothing but a frame in world space: Thorn sets and reads it relative to the
// script frame. A Thorn_MarkerReference (operator new(0x70), ctor FUN_00453910, identity vtable
// 0x005cda7c) forwards to a created marker of the same name in a running script; the spawn
// FUN_0044d810 finds it (FUN_00456790) and writes it into +0x6c.

#include "gf_thorn_component.h"

struct FThornMarker : FThornComponent
{
    // FUN_00453690 (thiscall, RET 0xc) -- base ctor, Pos 0, Orient identity.
    FThornMarker(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // ICompoundControl (0x005cd908 / 0x005cd90c)
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_004537d0
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_004538b0

    // IGeoTransformControl (0x005cd87c..0x005cd888)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00453750
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00453790
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00453830
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00453870

    // identity tail
    void* GetEngineInstance() override;                                  // 17 FUN_00450870

    Csys World; // +0x6c the marker in world space (position +0x6c, orientation +0x78)
};

struct FThornMarkerReference : FThornComponent
{
    // FUN_00453910 (thiscall, RET 0xc) -- base ctor, +0x6c = 0.
    FThornMarkerReference(Csys const& ScriptFrame, FGfScript* InOwner,
                          FThornEntitySnap const* Desc);

    // ICompoundControl
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00457c00
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00457c20

    // IGeoTransformControl
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00457bc0
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00457be0
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00453560
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00453580

    // identity tail
    void Stop() override;                                                // 14 LAB_00450cd0
    void* GetEngineInstance() override;                                  // 17 LAB_00453550

    FThornComponent* Target; // +0x6c the created marker, or null
};
