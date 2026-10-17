// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail "space environment FX" subsystem: freelancer.exe .text 0x00500000-0x00520000.
// Confirmed retail source file (embedded ERROR/WARNING format strings, .text:00502ba0+
// and .text:00502b00): "E:\FL\Scratch\Source\Client\Space\JumpEffect.cpp" -- this is
// retail's JumpGateEffect / JumpShipEffectDesc class pair.
//
// Evidence chain (see decomp-tracker notes on FUN_005020a0, FUN_00502ba0, FUN_00502b00,
// FUN_00504cf0/LAB_00505342, and freelancer.exe.c around line 165163+):
//   - FUN_005020a0 parses [JumpGateEffect]/[JumpShipEffect] sections of the per-system FX
//     INI, allocating small effect-node structs (operator_new(0x70) / operator_new(0x14)).
//     The port's LoadJumpGateEffectsIni() (space_scene_load.cpp) is an explicit stub that
//     only logs and never builds these nodes or calls RegisterSceneRenderable.
//   - FUN_00502ba0 is JumpGateEffect's constructor (embedded string "null solar passed to
//     JumpGateEffect constructor"): takes a Solar object, looks up a JumpGateEffectNode by
//     ID_String key (default "jump_effect_default" via CreateID) in the static node list
//     built by FUN_005020a0/LAB_00502000, then resolves hardpoints via FindHardpoint_PS.
//   - FUN_00502b00 lazily default-constructs a global JumpShipEffectDesc (embedded string
//     "no JumpShipEffectDesc was loaded, creating a default") -- confirms JumpShipEffectNode
//     is actually 5 consecutive ID_String fields (operator_new(0x14) = 5*4 bytes, each
//     cleared via ID_String::clear at offsets 0x0/0x4/0x8/0xc/0x10).
//   - FUN_00504cf0 (paired with ctor-ish LAB_00505342, both registered via
//     COMMON.DLL::NewSpatial) is the destructor of a "space env FX" renderable object
//     (Nebula/Clouds/ObjectFog/TexturePanels/Lightning/BackgroundLightning per
//     space_scene_load.cpp's System.ini section dispatch), sharing vtable PTR_LAB_005dad1c.
//   - FUN_00500a80/FUN_00500de0/FUN_005012e0/FUN_00501470/FUN_00501750/FUN_005018a0 and
//     friends implement resizable float-vector buffers (VC6 std::vector<float>-style: a
//     [begin,end,capacity] triple manipulated via FUN_005ad970 "capacity",
//     FUN_00537260 "grow/insert", FUN_005995c0 "shrink/destroy tail") filled with
//     rand()-jittered values -- the classic Freelancer "flicker" arrays used to animate
//     nebula/cloud/lightning color and vertex offsets over time.
//
// None of this renderable/particle-array engine exists in the port yet: the space scene
// loader defers the whole [Nebula]/[Clouds]/[ObjectFog]/[TexturePanels]/[Lightning]/
// [BackgroundLightning]/[JumpGateEffect]/[JumpShipEffect] System.ini surface (see
// space_scene_load.cpp, LoadJumpGateEffectsIni() and the System.ini header dispatch).
// This header is a placeholder so a future pass has named types to hang real ports off
// instead of re-deriving the struct layout from scratch every time.

#include "common_id.h"
#include "common_archetype.h" // CacheString

// One glow ring of a [JumpGateEffect] section (0xc bytes, five of them). FUN_00502a80 is its
// constructor, not a destructor: ID_String::clear on +0, CacheString::clear on +4, return
// this (the loader FUN_005020a0 runs it on each of the five, 0x00502192). Keys:
// glow_ring_effect (+0, CreateID), glow_ring_hp (+4, StringAlloc), glow_create_time (+8).
struct FJumpGateGlowRing
{
    ID_String Effect;  // +0x00
    CacheString Hp;    // +0x04
    float CreateTime;  // +0x08

    FJumpGateGlowRing()
    {
        Effect.clear();
        Hp.clear();
    }
};
static_assert(sizeof(FJumpGateGlowRing) == 0xc, "glow ring 0xc");

// The payload of one DAT_00674fd8 list node (the node is operator new(0x70): next, prev, then
// this 0x68-byte record, filled by FUN_005020a0 from a [JumpGateEffect] section and copied in
// by FUN_005048a0). Offsets from the loader's stack record [ESP+0x24] and from the readers
// FUN_00502ba0 (+0x00, +0x04.., +0x40) and FUN_00503440 (+0x44..+0x63).
struct FJumpEffectDesc
{
    ID_String Nickname;               // +0x00  nickname
    FJumpGateGlowRing Rings[5];       // +0x04
    int RingCount;                    // +0x40  min of the three per-ring key counts
    float JumpOutTime;                // +0x44  jump_out_time
    float JumpOutTunnelTime;          // +0x48  jump_out_tunnel_time
    float JumpInTunnelTime;           // +0x4c  jump_in_tunnel_time
    float JumpInTime;                 // +0x50  jump_in_time
    float KillTimeBeforeDone;         // +0x54  kill_time_before_done
    ID_String JumpTunnelEffect;       // +0x58  jump_tunnel_effect
    ID_String JumpTunnel;             // +0x5c  jump_tunnel (a GateTunnel id)
    unsigned char JumpAmbient[3];     // +0x60  jump_ambient (three ftol'd floats)
    unsigned char JumpBackgroundColor[3]; // +0x63 jump_background_color
    unsigned char Pad66[2];
};
static_assert(sizeof(FJumpEffectDesc) == 0x68, "jump effect desc 0x68 (list node 0x70)");

// JumpShipEffectDesc (retail name -- see FUN_00502b00's embedded warning string).
// operator_new(0x14) = 5 consecutive ID_String fields, each ID_String::clear()'d by
// FUN_00502b00's default-construction path. The [JumpShipEffect] loader FUN_005020a0 fills
// Ids[1] jump_out_effect (0x0050260e), Ids[2] jump_in_effect (0x0050263c), Ids[3]
// jump_done_effect_player (0x00502667), Ids[4] jump_done_effect_nonplayer (0x00502692);
// Ids[0] is never written by it (meaning unknown, TODO).
struct JumpShipEffectDesc
{
    ID_String Ids[5];

    // Retail's lazy-default-construct path (FUN_00502b00) explicitly clears all 5 ids
    // rather than relying on ID_String's (trivial) default state.
    JumpShipEffectDesc()
    {
        for (int Index = 0; Index < 5; ++Index)
            Ids[Index].clear();
    }
};

// --- batch47a additions: further System.ini "space env fx" descriptor sections in the same
// 0x00500000-0x00520000 range, all still confirmed-but-unported. ---
//
// FUN_0050b2d0 (a per-frame Draw()-style helper of the JumpGateEffect family) walks fixed
// {type,value,next} triples at DAT_005db108/DAT_005db140/DAT_005db178 (SetTextureStageState,
// one table per texture stage) and DAT_005db0d8 (SetRenderState) through the global Pipeline
// object (Pipeline_exref), using slot offsets 0xf8/0x108/0xf0/0x110 -- these match
// kPipelineSetTextureStageState/kPipelineSetTextureStageTexture/kPipelineSetRenderState/
// kPipelineVerifyState in src/include/render/RenderPipeline.h EXACTLY. This *confirms* (not just
// suspects, per batch46a's open question) that this whole area's Draw() methods are ordinary
// Pipeline consumers, structurally identical to the already-ported PipelineApplyRsTable/
// PipelineApplyTssTable table-walk helpers -- only the owning renderable class/vtable and its
// state tables are unported, not the Pipeline layer itself.
//
// "[spines]" (Nebula lightning-bolt spine) descriptor -- FUN_0050fba0 parses this System.ini
// section by nickname (is_header("spines"), is_value("nickname") + stricmp). Confirmed fields
// (offsets relative to the target object, via its extraout_ECX-based stores):
struct NebulaSpineDesc
{
    // +0x10 CacheString Shape;  // "shape=" (FUN_00417c50 = shape-id hash; zeroed when the key's value is empty)
    // +0x38 float MinRadius;    // "min_radius="
    // +0x3c float MaxRadius;    // "max_radius="
    // "radius_scale=" (default 1.0) and "spine=" (5 floats/entry, forms an animation/keyframe
    // list) are also parsed but not yet mapped to stable offsets.
};

// "[star_glow]" descriptor -- FUN_0050c950. Owning class is StarSphere (PTR_FUN_005db658).
// Port: src/freelancer/Client/star_sphere.h (FStarGlowDesc) + star_sphere.cpp.
struct StarGlowDesc
{
    // See FStarGlowDesc — shape length@+0 / name@+4 / scale@+0x24 / colors / alpha@+0x40.
};

// "[lens_glow]" descriptor -- FUN_0050d420 into SunEffect body (+0x288..+0x2f4).
// Port: LensGlowLoadFromIni / FSunEffectBody in star_sphere.cpp.
struct LensGlowDesc
{
    // Embedded in FSunEffectBody Raw[0x30c]; not a standalone allocation.
};
//
// FUN_00510160 / FUN_0050da20 / FUN_0050e6f0 — Draw helpers; class live, draw still partial.
// See star_sphere.cpp FStarSphereVt slots 4–6.
