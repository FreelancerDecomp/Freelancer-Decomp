// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The blinking light ("FlashLight") a light equipment item on a room prop carries, and the set
// of them a Gf::ObjectProp keeps at +0x74. Retail freelancer.exe 0x00557700..0x005585d0,
// Client\supt\LightsCamera.cpp's neighbourhood (no path string of its own).

#include "common_csys.h"

#include <list>

struct ICamera;
class HardpointSummary;

namespace Archetype
{
    struct ICliObj;
}

// sizeof 0x7c (operator new(0x7c) at 0x00558351). Built from a CliEQLight companion
// (Archetype::Light's Root+0x04, client_cliobj.cpp FCliEQLight).
struct FFlashLight
{
    // FUN_00557700 (thiscall, RET 4).
    explicit FFlashLight(Archetype::ICliObj* Companion);

    // FUN_00557630 (thiscall, RET 0x1c) -- the explicit form the room flashlights use
    // (FUN_0044a3a0): Color / MinColor by pointer, then period, starting phase, blink duration,
    // bulb size and glow size. The flare cone is left 0.
    FFlashLight(Vector const* InColor, Vector const* InMinColor, float Period, float InPhase,
                float BlinkDuration, float InBulbSize, float InGlowSize);

    // FUN_00557810 (thiscall, RET 4) -- +0x4c = Frame's matrix (its +0x0c), +0x70 its
    // position (its +0x00).
    void SetFrame(Csys const& Frame);

    // FUN_00557860 (thiscall, RET 4) -- advance the blink phase by Delta * Rate (wrapped into
    // [0, 1)) and rebuild both colours from it.
    void Update(float Delta);

    // FUN_005579b0 (thiscall, RET 4) -- the first flare pass (the "shine" texture): a
    // camera-facing quad of half-size GlowSize in GlowColor, scaled by the flare cone term when
    // +0x44 > +0x48. Nothing for a black GlowColor.
    void DrawShine(ICamera* Camera);
    // FUN_00557d70 (thiscall, RET 4) -- the second pass (the "b" texture): a camera-facing quad
    // of half-size BulbSize in BlendColor. Nothing for a black BlendColor.
    void DrawBulb(ICamera* Camera);

    Vector Color;              // +0x00 companion +0x54 "color"
    Vector MinColor;           // +0x0c companion +0x6c "min_color"
    Vector BlendColor;         // +0x18 MinColor + t * (Color - MinColor)
    Vector GlowColor;          // +0x24 t * Color
    float Rate;                // +0x30 1 / (avg_delay jittered by rand to 0.9 .. 1.1)
    float Phase;               // +0x34 rand() / 32768 at construction
    float BulbSize;            // +0x38 companion +0x78 "bulb_size"
    float GlowSize;            // +0x3c companion +0x7c "glow_size"
    float BlinkFraction;       // +0x40 min(blink_duration, period) / period, 1 when no period
    float FlareConeOuterCos;   // +0x44 companion +0x8c "flare_cone" (second value)
    float FlareConeInnerCos;   // +0x48 companion +0x90 "flare_cone" (first value)
    Matrix Orient;             // +0x4c
    Vector Pos;                // +0x70
};

static_assert(sizeof(FFlashLight) == 0x7c, "FlashLight sizeof 0x7c");

// The heap pair each list node of FFlashLightSet holds (operator new(8) at 0x0055832c).
struct FFlashLightEntry
{
    FFlashLight* Light;        // +0x00
    int HardpointIndex;        // +0x04 index into the owner's HardpointSummary
};

// Retail: an allocator byte, then a VC6 std::list<FFlashLightEntry*> (head +0x04, size +0x08).
struct FFlashLightSet
{
    // FUN_00558180 (thiscall, no arguments).
    FFlashLightSet();
    // FUN_005581b0 -- delete every light and entry, then the list.
    ~FFlashLightSet();

    // FUN_005582a0 -- delete every light and entry and empty the list.
    void Clear();
    // FUN_00558310 (thiscall, RET 8) -- a new light for Companion bound to HardpointIndex.
    void Add(Archetype::ICliObj* Companion, int HardpointIndex);
    // FUN_00558470 (thiscall, RET 8) -- place every light on its hardpoint's current world
    // frame and advance it by Delta.
    void Update(HardpointSummary const* Hardpoints, float Delta);
    // FUN_00558580 (thiscall, RET 4) -- the first 0x20 lights into FUN_00558000.
    void Draw(ICamera* Camera);

    std::list<FFlashLightEntry*> Entries;
};

// FUN_00558000 (cdecl) -- the flashlight flares: the EFFECTS and FLASHLIGHTS options and a
// non-empty list, then two quad passes over it -- FUN_005579b0 with the "shine" texture, then
// FUN_00557d70 with the "b" texture -- between viewport / camera / state setup and the state
// restore. Callers: FUN_00558580 and the room draw FUN_0044b570 (RoomControl +0x81c / +0x101c).
void DrawFlashlightFlares(FFlashLight* const* Lights, int Count, ICamera* Camera);

// DAT_006799c0 / DAT_006799c4 -- the flare textures ("b" and "shine" in fx\efx.txm), texture
// records of ITextureLibrary2 slot 12. Null outside a base.
extern void* G_FlashlightBulbTexture;
extern void* G_FlashlightShineTexture;

// FUN_00557550 -- load both flare textures. Caller: FUN_005588a0 (entering a base).
void LoadFlashlightTextures();
// FUN_005575e0 -- release them (TextureLib slot 14) and clear the globals. Caller: FUN_00558a50.
void ReleaseFlashlightTextures();
