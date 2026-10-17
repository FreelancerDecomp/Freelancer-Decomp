// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Space\JumpEffect.cpp (path string .rdata 0x005da80c) -- the JumpShipEffect,
// the 0xdc-byte object a client Ship keeps at +0x188 while it jumps (FClientShipGameobj::
// Field_0x188). Built by FUN_00543530 (FUN_00503440(ship, gate solar)) or FUN_00543630
// (FUN_005038c0(ship)), destroyed by FUN_00503a80 + operator delete (FUN_005437d0 case 0,
// FUN_00543530, FUN_00543630 and the Ship dtor FUN_005444a0 at 0x0054456b).
//
// Ported here: both JumpShipEffect constructors (FUN_00503440, FUN_005038c0), its destructor
// FUN_00503a80, the camera-shake amplitude FUN_00503e20, the descriptor getter FUN_00502b00,
// and the JumpGateEffect constructor / destructor / ring (FUN_00502ba0, FUN_00502e40,
// LAB_00502de0, FUN_00502e00), FUN_00503380, the [JumpGateEffect]/[JumpShipEffect] loader
// FUN_005020a0 (its record copy FUN_005048a0 is the implicit copy) and the teardown
// FUN_00502aa0, and the four jump-state hooks FUN_00503ea0 / FUN_00504210 / FUN_00504470 /
// FUN_00504640 and the update FUN_00503cd0 (W3-A; called by the ship update FUN_00545050), and
// the JumpGateEffect update FUN_00502f00 (W3-A; called by the solar update FUN_00547880).

#include "common_csys.h"
#include "common_id.h"
#include "space_env_fx.h" // JumpShipEffectDesc (0x14, FUN_00503a50), FJumpEffectDesc

#include <cstddef>

struct ISoundSource;          // soundmanager.h
struct FParticleEffect;       // particle_effect.h
struct FGameLight;            // engine_light.h
struct FGateTunnel;           // gate_tunnel.h
class FClientShipGameobj;     // client_gameobj.h

// The 0x10-byte effect companion {slot0, ISoundSource*, particle, GameLight}. Its destructor is
// FUN_00502d90 (CliEffectCompanionClear): FUN_00503a80's EH states 3..0 run it on the four
// members in reverse order (0x00503c07, then inline 0x00503c0c.., 0x00503c43.., 0x00503c78..).
struct FJumpFxCompanion
{
    void* Slot0;               // +0x00
    ISoundSource* Sound;       // +0x04
    FParticleEffect* Particle; // +0x08
    FGameLight* Light;         // +0x0c

    FJumpFxCompanion() : Slot0(nullptr), Sound(nullptr), Particle(nullptr), Light(nullptr) {}
    ~FJumpFxCompanion();

    FJumpFxCompanion(FJumpFxCompanion const&) = delete;
    FJumpFxCompanion& operator=(FJumpFxCompanion const&) = delete;
};
static_assert(sizeof(FJumpFxCompanion) == 0x10, "effect companion 0x10");

// One glow ring of a JumpGateEffect (0x24 bytes, five at +0x2c). Constructor LAB_00502de0
// (byte +0 and dwords +4..+0x20 zeroed), destructor FUN_00502e00 (the companion at +0x14
// released: particle +0x1c, sound +0x18, light +0x20, then +0x14 = 0 -- FUN_00502d90's body).
struct FJumpGateRingFx
{
    unsigned char bCreated;  // +0x00  FUN_00502f00 sets it when the ring's effect is made
    unsigned char Pad01[3];
    float CreateTime;        // +0x04  descriptor ring +8
    Vector Position;         // +0x08  the ring hardpoint (FindHardpoint_PS, parent space)
    FJumpFxCompanion Fx;     // +0x14

    FJumpGateRingFx();
};
static_assert(sizeof(FJumpGateRingFx) == 0x24, "gate ring 0x24");

class FClientSolarGameobj; // client_gameobj.h

// The JumpGateEffect a jump gate / hole solar keeps at +0x204 (operator new(0xe0)): built by
// the solar notify (0x00547e1f) or, as a temporary, by FUN_00503440 (0x0050358a).
struct FJumpGateEffect
{
    // FUN_00502ba0 (thiscall, RET 4).
    explicit FJumpGateEffect(FClientSolarGameobj* Solar);
    // FUN_00502e40 (thiscall). Callers: the solar dtor 0x005476d3 / notify 0x005479da, and
    // inline in FUN_00503440 (0x005037ed..0x00503896).
    ~FJumpGateEffect();

    FJumpGateEffect(FJumpGateEffect const&) = delete;
    FJumpGateEffect& operator=(FJumpGateEffect const&) = delete;

    // FUN_00502f00 (thiscall, RET 4) -- the per-frame update. Caller: the solar update
    // FUN_00547880 (0x005478d6) when the solar has one. The clock runs until bFire; each ring
    // whose create time the clock has passed starts its effect once, at its hardpoint.
    void Update(float Dt);

    FJumpEffectDesc* Desc;             // +0x00  the DAT_00674fd8 record, or null
    FClientSolarGameobj* Solar;        // +0x04
    float Clock;                       // +0x08  FUN_00502f00 advances it
    Vector JumpOutPosition;            // +0x0c  the archetype's JumpOutHp (parent space)
    unsigned char bFire;               // +0x18  FUN_00503380 sets it
    unsigned char Pad19[3];
    FJumpFxCompanion Fx;               // +0x1c
    FJumpGateRingFx Rings[5];          // +0x2c
};
static_assert(offsetof(FJumpGateEffect, JumpOutPosition) == 0x0c, "JumpGateEffect +0x0c");
static_assert(offsetof(FJumpGateEffect, bFire) == 0x18, "JumpGateEffect +0x18");
static_assert(offsetof(FJumpGateEffect, Fx) == 0x1c, "JumpGateEffect +0x1c");
static_assert(offsetof(FJumpGateEffect, Rings) == 0x2c, "JumpGateEffect rings +0x2c");
static_assert(sizeof(FJumpGateEffect) == 0xe0, "JumpGateEffect operator_new(0xe0)");

// FUN_00503380 (thiscall) -- bFire = 1. Caller: solar notify 0x00547bda.
void JumpGateEffectFire(FJumpGateEffect* Gate);

struct FJumpShipEffect
{
    // FUN_005038c0 (thiscall, RET 4) -- the no-gate constructor.
    explicit FJumpShipEffect(FClientShipGameobj* Ship);
    // FUN_00503440 (thiscall, RET 8) -- the constructor for a jump through GateSolar (a jump
    // gate / hole solar gameobj). Caller: FUN_00543530 (0x00543595), not ported.
    FJumpShipEffect(FClientShipGameobj* Ship, FClientSolarGameobj* GateSolar);
    // FUN_00503a80 (thiscall, RET 0).
    ~FJumpShipEffect();

    FJumpShipEffect(FJumpShipEffect const&) = delete;
    FJumpShipEffect& operator=(FJumpShipEffect const&) = delete;

    // FUN_00503e20 (thiscall, RET 0) -- the camera-shake amplitude for the ship's jump state
    // (+0x68 -> +0x158), left on the FPU. Caller: HudCameraUpdateShake 0x0051dba8.
    double ShakeAmplitude() const;

    // The four state hooks FUN_005437d0 (FClientShipGameobj::SetJumpState) calls. Its case 3
    // calls a fifth, IDPMsgHandler::OnDisconnect (0x00401e90) -- a bare RET the linker folded
    // it into, so there is nothing to port for it.
    // FUN_00503ea0 (thiscall, RET 0), state 1: with a tunnel, tunnel state 1 and the tunnel
    // effect (+0x18) started at +0xa8 into FxB.
    void StartJumpOut();
    // FUN_00504210 (thiscall, RET 0), state 2: tunnel state 2; FxB moved onto the ship, and the
    // descriptor's jump_out_effect started at the ship into FxA and moved onto the ship too.
    void EnterTunnel();
    // FUN_00504470 (thiscall, RET 0), state 4: tunnel state 3; the descriptor's jump_in_effect
    // started at the ship into FxC and moved onto it; +0xd8 = 1.
    void StartJumpIn();
    // FUN_00504640 (thiscall, RET 0), state 5: tunnel state 0; the descriptor's jump done
    // effect (player or non-player) started on the ship into FxD.
    void FinishJump();

    // FUN_00503cd0 (thiscall, RET 4) -- the per-frame update. Caller: the ship update
    // FUN_00545050 at 0x0054536f (FClientShipGameobj::update), with its clamped step. States
    // 2/3 advance the tunnel; in state 4, once the clock passes JumpInTime - KillTimeBeforeDone,
    // the tunnel effect FxB is stopped and released and +0x6d latches.
    void Update(float Dt);

    JumpShipEffectDesc* Desc;        // +0x00  FUN_00502b00 / DAT_00674fd0
    // +0x04 .. +0x20 -- FUN_00503440 copies them from the gate's FJumpEffectDesc (+0x44..+0x63;
    // JumpOutTime is 0 when the gate's type has bit 0x800). FUN_00503e20 divides by JumpOutTime
    // in state 1 and by JumpInTime in state 4, against the clock at +0x70.
    float JumpOutTime;                 // +0x04  ctor 0
    float JumpOutTunnelTime;           // +0x08  ctor 5.0f
    float JumpInTunnelTime;            // +0x0c  ctor 5.0f
    float JumpInTime;                  // +0x10  FUN_005038c0 0.5f, FUN_00503440 0
    float KillTimeBeforeDone;          // +0x14  ctor 0.5f
    ID_String TunnelEffect;            // +0x18  clear(); FUN_00503440: jump_tunnel_effect
    unsigned char const* Ambient;      // +0x1c  &desc jump_ambient (FUN_00503440), else 0
    unsigned char const* Color;        // +0x20  &desc jump_background_color; FUN_00504880
    FGateTunnel* Tunnel;               // +0x24  the GateTunnel (FUN_004fedd0), player only
    FJumpFxCompanion FxA;              // +0x28
    FJumpFxCompanion FxB;              // +0x38
    FJumpFxCompanion FxC;              // +0x48
    FJumpFxCompanion FxD;              // +0x58  also linked into the ship's +0x28 effect list
    FClientShipGameobj* Ship;          // +0x68
    unsigned char Field_0x6c;          // +0x6c  ctor 0
    unsigned char Field_0x6d;          // +0x6d  ctor 0; FUN_00503cd0 sets 1, FUN_00503ea0 0
    unsigned char Pad6e[2];
    float Field_0x70;                  // +0x70  ctor 0; the effect's clock (FUN_00503e20)
    // +0x74 / +0x80 -- where the ship comes out of the jump: FUN_00543630 copies 3 dwords of
    // its Vector argument to +0x74 (0x0054366b..0x00543679 / 0x005436ff..0x0054370d) and 9 of
    // its Matrix argument to +0x80 (MOVSD.REP ECX=9); FUN_005437d0 case 4 hands both to
    // CShip::jump_in / CObject::beam_object as (Vector const&, Matrix const&).
    Vector JumpInPosition;             // +0x74
    Matrix JumpInOrientation;          // +0x80
    unsigned Field_0xa4;               // +0xa4  FUN_00503440: the gate CSolar's +0x1b4 (player)
    Transform Field_0xa8;              // +0xa8  identity; FUN_00503440: the gate hardpoint
    unsigned char Field_0xd8;          // +0xd8  ctor 0; FUN_00503cd0 clears it
    unsigned char Padd9[3];
};
static_assert(offsetof(FJumpShipEffect, TunnelEffect) == 0x18, "JumpShipEffect +0x18");
static_assert(offsetof(FJumpShipEffect, Color) == 0x20, "JumpShipEffect colour +0x20");
static_assert(offsetof(FJumpShipEffect, FxA) == 0x28, "JumpShipEffect +0x28");
static_assert(offsetof(FJumpShipEffect, FxD) == 0x58, "JumpShipEffect +0x58");
static_assert(offsetof(FJumpShipEffect, Ship) == 0x68, "JumpShipEffect ship +0x68");
static_assert(offsetof(FJumpShipEffect, Field_0x70) == 0x70, "JumpShipEffect +0x70");
static_assert(offsetof(FJumpShipEffect, JumpInPosition) == 0x74, "JumpShipEffect +0x74");
static_assert(offsetof(FJumpShipEffect, JumpInOrientation) == 0x80, "JumpShipEffect +0x80");
static_assert(offsetof(FJumpShipEffect, Field_0xa4) == 0xa4, "JumpShipEffect +0xa4");
static_assert(offsetof(FJumpShipEffect, Field_0xa8) == 0xa8, "JumpShipEffect transform +0xa8");
static_assert(offsetof(FJumpShipEffect, Field_0xd8) == 0xd8, "JumpShipEffect +0xd8");
static_assert(sizeof(FJumpShipEffect) == 0xdc, "JumpShipEffect operator_new(0xdc)");

// FUN_00502b00 (cdecl) -- DAT_00674fd0, the JumpShipEffectDesc the [JumpShipEffect] section
// of the jump-effect INI (FUN_005020a0, not ported) loads; the first call without one warns
// and news a default (five cleared ids).
JumpShipEffectDesc* JumpShipEffectDescGet();

// FUN_005020a0 (cdecl, no arguments; caller: the space init FUN_0054ab40 at 0x0054ac15) --
// reads the [Data] jump_effect INI: every [JumpGateEffect] section is appended to the
// DAT_00674fd8 record list, a [JumpShipEffect] section replaces DAT_00674fd0.
void JumpEffectIniLoad();

// FUN_00502aa0 (cdecl, no arguments; caller: the space teardown at 0x0054afb1) -- empties the
// DAT_00674fd8 list and frees DAT_00674fd0.
void JumpEffectDescsClear();
