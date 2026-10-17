// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIWinObject — PAGE_*.txt mesh widget (RTTI .?AUUIWinObject@@ : WinObject : Win3D).
//
// Retail:
//   WINFACTORY<UIWinObject> DAT_0067dac0, static init LAB_0059c2a0 → FUN_0059c8d0
//   Create FUN_0059c940: operator_new(0x100), Win3D ctor FUN_004b6790, WinObject part
//   (mesh holder +0x7c FUN_00420d40 / +0x90 FUN_00492250, holder vftable 0x005d2efc,
//   holder+0x10 = -1, colour +0xa5, byte +0xa4 &= 0xf8), then +0xb8 FUN_004215b0 identity
//   3x3 + zero translation, vftable 0x005e5d34.
//   vftable 0x005e5d34 overrides of WinObject 0x005d709c:
//     slot 0  FUN_0059ce20  scalar deleting destructor (dtor FUN_0059cf70 = WinObject's)
//     slot 5  LAB_0059ab10  SetPosition
//     slot 8  LAB_0059a900  SetTransform
//     slot 9  FUN_0059a760  Create
//     slot 13 0x0059ab50    draw: debug frame (dead — DAT_0067dbc4 has no writer), then
//                           WinObject draw FUN_0059f120
//
// The WinObject part (mesh holder, +0xa4 draw flags, +0xa5 tint, slots 10 / 11 / 13) is the
// WinObject base (win_object.h); the debug frame of slot 13 is dead, so Draw is WinObject's.

#include "win_object.h"

#include <stddef.h>

class UiWinObject : public WinObject
{
public:
    UiWinObject();
    ~UiWinObject() override;

    // slot 5 (+0x14) — LAB_0059ab10.
    void SetPosition(float const* NewPosition) override;
    // slot 8 (+0x20) — LAB_0059a900.
    void SetTransform(float const* Transform) override;
    // slot 9 (+0x24) — FUN_0059a760: false when Payload is null or the mesh load fails.
    bool Create(Win3D* NewParent, void* Payload) override;

    FClientMeshHolder const& Mesh() const { return MeshHolder; }

private:
    float Width_;                           // +0xac — payload +0x00
    float Height_;                          // +0xb0 — payload +0x04
    float Depth_;                           // +0xb4 — payload +0x0c (5th float)
    float PageBlock_[12];                   // +0xb8 — slot 8 argument copy (ctor: identity)
    float Origin_[3];                       // +0xe8 — slot 8: Block * (0, 0, 0)
    float Corner_[3];                       // +0xf4 — slot 8: Block * (Width, -Height, 0)
};

static_assert(sizeof(UiWinObject) == 0x100, "retail UIWinObject is 0x100 bytes");
