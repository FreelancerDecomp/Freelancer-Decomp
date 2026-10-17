// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// LightsCamera particle holder (E:\FL\Scratch\Source\Client\supt\LightsCamera.cpp) -- the two
// words an Alchemy particle effect is kept in by its owners: Thorn_Psys (+0x6c, ScriptPsys.cpp)
// and the Gf ObjectShip's engine effects (callers 0x00446c83 / 0x00446ca5 / 0x00446e9d /
// 0x00447609). The three functions below are its whole interface.

struct FVisEffectArch;
struct ICamera;
struct IFxEffect;

struct FLightsCameraParticles
{
    // FUN_00557360 (thiscall, RET 4). Without DALib::Alchemy: false, nothing touched. Otherwise
    // Vis is kept and AddRef'd (FUN_0042e080), an "FxNode" is created through DALib::Dacom
    // (AGGDESC size 8), DALib::FxEffectLibrary slot 6 builds the effect for Vis's effect_crc
    // into Effect, and the node is attached under Alchemy's root (slots 25 / 20). Any failure
    // FDUMPs "ParticleEffect could not create Alchemy effect" and drops Vis again. The node
    // reference is released on both paths. True only on success.
    bool Create(FVisEffectArch* InVis);

    // FUN_005574a0 (thiscall). With an effect: its slot 13 with 7, Release, Effect = 0, and
    // Vis released (FUN_0042e160) and cleared. Without one, nothing.
    void Destroy();

    // FUN_005574d0 (thiscall, RET 4). With an effect and both the EFFECTS (0x25) and PARTICLES
    // (0x26) game options on: the effect's slot 4 (0, &scratch), then slot 15 its bounding
    // sphere; when that succeeds and the sphere is in Camera's view (FUN_0041c8d0), slot 9 draws
    // it with Camera.
    void Draw(ICamera* Camera);

    IFxEffect* Effect;   // +0x00
    FVisEffectArch* Vis; // +0x04
};
