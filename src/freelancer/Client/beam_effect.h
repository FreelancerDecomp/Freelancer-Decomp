// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "effect_base.h"
#include "x86math.h"

#include <cstddef>

// Retail BeamEffect — RTTI ".?AUBeamEffect@@", vtable PTR_LAB_005d9dac
// (Client\space\Effect.cpp). Emits camera-facing cross-billboard laser geometry
// into G_EffectVertexBatch (DAT_00674c7c). Texture nickname "code_beam".

// Beam archetype blob (FUN_0042e430 / FUN_0042e4a0 / FUN_0042e850) — sizeof 0x80.
// Pointed by FBeamEffect::Arch. INI keys → offsets; geometry names kept where
// builders already read them (SegmentMode / UvMode* = type / head|trail_texture).
struct FBeamEffectArch
{
    char* Nickname;            // +0x00  StringAlloc'd (CacheString in retail)
    int SegmentMode;           // +0x04  0=BeamSpear → single, 1=BeamBolt → multi
    int UvModeV;               // +0x08  head_texture: star=1 → V∈{0,.5}, else {.5,1}
    int UvModeU;               // +0x0C  trail_texture: thin=1 → U∈{0,.45}, else {.55,1}
    float ColorCoreR;          // +0x10  tip_color
    float ColorCoreG;          // +0x14
    float ColorCoreB;          // +0x18
    float ColorMidR;           // +0x1C  tail_color
    float ColorMidG;           // +0x20
    float ColorMidB;           // +0x24
    float ColorOuterR;         // +0x28  core_color
    float ColorOuterG;         // +0x2C
    float ColorOuterB;         // +0x30
    float ColorBand1R;         // +0x34  sec_core_color
    float ColorBand1G;         // +0x38
    float ColorBand1B;         // +0x3C
    float ColorTipR;           // +0x40  outter_color
    float ColorTipG;           // +0x44
    float ColorTipB;           // +0x48
    float ColorBand2R;         // +0x4C  sec_outter_color
    float ColorBand2G;         // +0x50
    float ColorBand2B;         // +0x54
    float IntensityScale0;     // +0x58  head_brightness
    float IntensityScale1;     // +0x5C  trail_brightness
    float Radius0;             // +0x60  tip_length
    float Radius1;             // +0x64  tail_length
    float RadiusMid;           // +0x68  core_length
    float RadiusNear;          // +0x6C  core_width
    float SecCoreWidth;        // +0x70  sec_core_width (FUN_0042e920 Bolt clamp)
    float RadiusTip;           // +0x74  head_width
    float RadiusNearAlt;       // +0x78  flash_size
    float Pad7C;               // +0x7C  defaulted 1.0 in FUN_0042e430
};
static_assert(sizeof(FBeamEffectArch) == 0x80, "BeamEffectArch 0x80");

// Retail object through FUN_004f9540's last write at +0x80.
// Transform at +0x10; BaseWatcher at +0x44 (FUN_004f9450 ADD ECX,0x44).
// Link node at +0x60/+0x64/+0x68 (EffectTypeListLink); type id at +0x5C.
struct FBeamEffect : FEffectBase
{
    explicit FBeamEffect(unsigned TypeId);
    unsigned char Start(FEffectArch* EffectArch) override;
    void Kill() override;
    float GetWorldPos(FVector* Out) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
    unsigned char BuildGeometry(FSpaceCamera* Cam) override;
    ~FBeamEffect() override;

    FBeamEffectArch* Arch;         // +0x6C  from EffectArch.VisBeam
    unsigned char Flags70;         // +0x70
    unsigned char Pad71[3];
    float Float74;                 // +0x74  = 50.0
    float Progress78;              // +0x78  = -1.0
    float Float7C;                 // +0x7C  = -1.0
    float ProgressDenom80;         // +0x80  = 0.1
};

static_assert(sizeof(FBeamEffect) == 0x84, "BeamEffect 0x84");
static_assert(offsetof(FBeamEffect, LocalXform) == 0x10, "xform @ +0x10");
static_assert(offsetof(FBeamEffect, Watcher) == 0x44, "BaseWatcher @ +0x44");
static_assert(offsetof(FBeamEffect, RefCount) == 0x58, "ref @ +0x58");
static_assert(offsetof(FBeamEffect, EffectTypeId) == 0x5c, "type @ +0x5c");
static_assert(offsetof(FBeamEffect, LinkPrev) == 0x60, "link @ +0x60");
static_assert(offsetof(FBeamEffect, Arch) == 0x6c, "Arch @ +0x6c");
static_assert(offsetof(FBeamEffect, Flags70) == 0x70, "Flags @ +0x70");

struct FEffectArch; // vis_effect_arch.h
struct FSpaceCamera; // space_camera.h

// FUN_004f9540 — construct; increments DAT_00674f58.
void BeamEffectConstruct(FBeamEffect* This, unsigned TypeId);

// FUN_004f9510 (slot +0x20) around FUN_004f9450 — live count--, BaseWatcher::set(nullptr),
// FreeMemory&1 → Heap::Free on the 0x84 pool (EffectFree). No unlink: the bucket walk does it.
void BeamEffectDestroy(FBeamEffect* This, int FreeMemory);

// LAB_004f9600 — vtable[0] start(EffectArch*): VisBeam → Arch, type=0x2c, EffectTypeListLink.
unsigned char BeamEffectStart(FBeamEffect* This, FEffectArch* EffectArch);

// LAB_004f97a0 — vtable[+0x04]: arm death / freeze + Float7C clamp to DAT_006129cc.
void BeamEffectArmDeath(FBeamEffect* This);

// LAB_004f9480 — vtable[+0x08]: world translation → Out; returns Float74 on ST0.
float BeamEffectGetWorldPos(FBeamEffect* This, FVector* Out);

// LAB_004f9980 — vtable[+0x14]: frustum / pbubble visibility → Flags70 bit0.
unsigned char BeamEffectUpdateVisibility(FBeamEffect* This, float Dt, FSpaceCamera* Cam);

// LAB_004f9c20 — vtable[+0x18]: Progress78 / Float7C lifetime; 0 → death (Flags08 bit1).
unsigned char BeamEffectTickDraw(FBeamEffect* This, float Dt, FSpaceCamera* Cam);

// FUN_004f7d20 — attach Watchable (+8) + EngineIndex + local transform copy.
void BeamEffectAttach(FBeamEffect* This, void* WatchableOwner, int EngineIndex,
                      FTransform const* LocalXform);

// FUN_004f7a80 / FUN_004f7a90
void BeamEffectAddRef(FBeamEffect* This);
int BeamEffectRelease(FBeamEffect* This);

// LAB_004fa0a0 — vtable slot 7 (+0x1c). Stack arg is Cam (unused for T/Intensity).
unsigned char BeamEffectBuildGeometry(FBeamEffect* This, FSpaceCamera* Cam);

// FUN_004fa270 — single-segment cross-billboard builder.
int BeamEffectBuildSingleSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                 float Intensity);

// FUN_004fb1e0 — multi builder; listing-verified (ESP+0x94 = B4 via SUB ESP alias;
// UvU1-bits-as-index is a retail bug, reproduced).
int BeamEffectBuildMultiSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                float Intensity);

extern int G_BeamEffectLiveCount; // DAT_00674f58
extern void* G_BeamEffectTexture; // DAT_00674f54 — FTexShape* from G_EffectShapeDB.Find("code_beam")
