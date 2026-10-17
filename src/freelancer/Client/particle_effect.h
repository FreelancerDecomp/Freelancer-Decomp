// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "beam_effect.h"
#include "effect_base.h" // FEffectBase (FCameraParticles head)
#include "vis_effect_arch.h"
#include "x86math.h"

#include "alchemy.h"
#include "common_goods.h"

#include <cstddef>

struct FGameLight; // engine_light.h

// Retail ParticleEffect — RTTI / Effect.cpp. Vtable PTR_FUN_005d9ad4.
// Same 0x84 footprint as BeamEffect, but +0x04 = VisEffect*, +0x6c = IFxEffect*,
// +0x70 = FxNode*, +0x7c = Alchemy flags (bit2 created, bit3 ale loaded, bit4 start).

struct FParticleEffect : FEffectBase
{
    explicit FParticleEffect(unsigned TypeId);
    unsigned char Start(FEffectArch* EffectArch) override;
    void Kill() override;
    float GetWorldPos(FVector* Out) override;
    void SetIntensity(float Intensity) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
    unsigned char BuildGeometry(FSpaceCamera* Cam) override;
    ~FParticleEffect() override;
    virtual void SetLod(float Lod);

    IFxEffect* FxEffect;           // +0x6C
    IDAComponent* FxNode;          // +0x70
    float Radius74;                // +0x74  from EffectTypeInfo.Radius
    float RunTime78;               // +0x78  from EffectTypeInfo.RunTime
    unsigned char Flags7C;         // +0x7C  bit2=ale.eff, bit3=ale loaded, bit4=start
    unsigned char Pad7D[3];
    float Float80;                 // +0x80  = 1.0; the ALCHEMY_LOD blend (FUN_0042dcd0)
};

static_assert(sizeof(FParticleEffect) == 0x84, "ParticleEffect 0x84");
static_assert(offsetof(FParticleEffect, Vis) == 0x04, "Vis @ +0x04");
static_assert(offsetof(FParticleEffect, FxEffect) == 0x6c, "FxEffect @ +0x6c");
static_assert(offsetof(FParticleEffect, FxNode) == 0x70, "FxNode @ +0x70");
static_assert(offsetof(FParticleEffect, Flags7C) == 0x7c, "Flags7C @ +0x7c");

// NPC cruise exhaust (FUN_00528980 local_58): sizeof 0x90 = ParticleEffect + float@+0x84.
// Vtable PTR_FUN_005dd144 — Start/ArmDeath shared with trail; destroy = FUN_00528cc0
// (frees 0x90); slots +0x08/+0x0c/+0x14 override radius/visibility for Scale@+0x84.
struct FParticleEffectCruise : FParticleEffect
{
    FParticleEffectCruise();
    float GetWorldPos(FVector* Out) override;
    void SetIntensity(float Scale) override;
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    float CruiseScale;         // +0x84  = 1.0f at factory; FUN_004f92f0 setter
    unsigned char Pad88[8];    // +0x88 … +0x8f — retail alloc 0x90
};

static_assert(sizeof(FParticleEffectCruise) == 0x90, "ParticleEffectCruise 0x90");
static_assert(offsetof(FParticleEffectCruise, CruiseScale) == 0x84, "CruiseScale @ +0x84");

struct FJumpParticleEffect : FParticleEffect
{
    FJumpParticleEffect() : FParticleEffect(0x25u) {}
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
};

struct FParticleTunnel : FParticleEffect
{
    FParticleTunnel() : FParticleEffect(0) {}
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
};

// FUN_004f8060 — construct; param = initial EffectTypeId (0 from factory).
void ParticleEffectConstruct(FParticleEffect* This, unsigned TypeId);

// FUN_004f8110 — Slot13(7)+Release at +0x6c; Alchemy DetachNode + Release at +0x70.
void ParticleEffectCleanupAlchemy(FParticleEffect* This);

// FUN_004f8360 — CreateInstance FxNode + LoadAle + CreateEffect + AttachNode(root).
unsigned char ParticleEffectCreateAlchemyNodes(FParticleEffect* This);

// FUN_004f8160 — vtable[0] start(EffectArch*).
unsigned char ParticleEffectStart(FParticleEffect* This, FEffectArch* EffectArch);

// LAB_004f84d0 — vtable slot +4 (stop). Without an IFxEffect: link, FUN_004f7db0, dead.
// With one: link, freeze to world (dead too when the watched object is gone), pin the run time
// at +0x78 into [0, 1] (NaN kept), then IFxEffect vt+0x34(1) so the effect winds down.
void ParticleEffectArmDeath(FParticleEffect* This);

// LAB_004f8c20 — vtable[3] (+0x0c): intensity clamped to [0, 1] into the IFxEffect's vt+0x18;
// no-op without an IFxEffect. SpaceDust (FUN_0054beb0) drives its dust effects through it.
void ParticleEffectSetIntensity(FParticleEffect* This, float Intensity);

struct FSpaceCamera;

// FUN_004f86c0 — vtable[+0x14]: particles option, the watched ship/solar gates (cloaked
// CEqObj, field controller DAT_00675244 and its radius), perf priority, near plane, pbubble,
// ALCHEMY_LOD blend into +0x80 (FUN_0042dcd0), side planes -> bit 0 of +0x7c, returned.
unsigned char ParticleEffectUpdateVisibility(FParticleEffect* This, float Dt, FSpaceCamera* Cam);

// FUN_004f8a20 — vtable[+0x18]: run-time countdown / ale-finished test (dead on either), then
// FUN_004f8360 and, with an IFxEffect, SetLod(+0x80), transform, update and tick by Dt.
unsigned char ParticleEffectTickDraw(FParticleEffect* This, float Dt, FSpaceCamera* Cam);

// FUN_004f8be0 — vtable[+0x1c]: clear the visibility bit; IFxEffect +0x24(Cam) (the draw).
unsigned char ParticleEffectBuildGeometry(FParticleEffect* This, FSpaceCamera* Cam);

// LAB_004f8c80 — vtable[+0x24]: LOD clamped to [0, 1] into IFxEffect +0x38; warns without one.
void ParticleEffectSetLod(FParticleEffect* This, float Lod);

// FUN_004f91c0 — cruise vtable[+0x08]: radius = +0x84 * +0x74, at least 10; world position.
float ParticleEffectCruiseGetWorldPos(FParticleEffectCruise* This, FVector* Out);

// FUN_004f92f0 — cruise vtable[+0x0c]: +0x84 = Scale, then the SetIntensity clamp into
// IFxEffect +0x18. Tradelane rings drive it (FUN_00514050).
void ParticleEffectCruiseSetScale(FParticleEffectCruise* This, float Scale);

// FUN_004f9270 — cruise vtable[+0x14]: FUN_004f86c0, then a hidden effect fades +0x80 by
// Dt * 0.5 while staying visible.
unsigned char ParticleEffectCruiseUpdateVisibility(FParticleEffectCruise* This, float Dt,
                                                   FSpaceCamera* Cam);

// FUN_004f7f70 — destructor body (alchemy cleanup, base vptr, watcher cleared).
void ParticleEffectDestruct(FParticleEffect* This);

// FUN_004f8030 — slot +0x20 of PTR_FUN_005d9ad4: destruct; FreeMemory&1 → Heap::Free on the
// 0x84 pool (EffectFree).
void ParticleEffectDestroy(FParticleEffect* This, int FreeMemory);

// FUN_00528cc0 — slot +0x20 of PTR_FUN_005dd144 (cruise): destruct; FreeMemory&1 →
// FUN_004f79c0(this, 0x90), i.e. free().
void ParticleEffectCruiseDestroy(FParticleEffect* This, int FreeMemory);

// FUN_004f7a80 / FUN_004f7a90 (Release deletes through the object's own slot +0x20).
void ParticleEffectAddRef(FParticleEffect* This);
int ParticleEffectRelease(FParticleEffect* This);

// Shared attach FUN_004f7d20 (same layout as BeamEffect).
void ParticleEffectAttach(FParticleEffect* This, void* WatchableOwner, int EngineIndex,
                          FTransform const* LocalXform);

// LAB_004f6e70 — EffectAlloc(0x84) (Heap pool) + Construct(0).
FParticleEffect* ParticleEffectCreate();

// FUN_00528980 NPC-cruise arm — EffectAlloc(0x90) (malloc) + Construct(0) + CruiseScale=1 +
// PTR_FUN_005dd144. Destroy path: FUN_00528cc0 (ParticleEffectCruiseDestroy).
FParticleEffect* ParticleEffectCreateCruise();

// LAB_005033a0 (slot 0 of the jump-effect factory, .rdata 005daab4) -- EffectAlloc(0x84) (the
// Heap pool), FUN_004f8060(0x25), then the jump particle's own vtable PTR_FUN_005daac4: the
// particle's table with slot +0x14 LAB_005033e0 (bit 0 of +0x7c raised, never culled) and slot
// +0x20 FUN_005033f0 (destruct, then FUN_004f79c0(this, 0x84)).
FParticleEffect* ParticleEffectCreateJump();

// LAB_00513440 (slot 0 of the tradelane factory DAT_006751cc, .rdata 005db760) -- EffectAlloc(0x84)
// (the Heap pool), then LAB_005134a0: FUN_004f8060(0) and the ParticleTunnel vtable
// PTR_FUN_005db770 (RTTI .?AUParticleTunnel@@): the particle's table with +0x14 FUN_005134c0 (bit 0
// of +0x7c = the drawing camera is a cockpit camera), +0x18 LAB_00513510 (follow the camera's
// position, then the particle tick) and +0x20 FUN_005033f0.
FParticleEffect* ParticleTunnelCreate();

// Retail CameraParticles (RTTI .?AUCameraParticles@@ at vtable PTR_LAB_005d9d84[-4], nine
// slots) -- the particle of the tradelane factory DAT_00674c5c. No Alchemy: it is a white
// screen flash. Start fades the screen out to white when the listener camera is near the effect;
// the draw tick counts the timer down, fades back in and kills the effect. 0x78 bytes: the
// Effect head (FUN_004f79f0's body inlined into the ctor FUN_004f8d00, type id 0x2c), then three
// fields of its own.
struct FCameraParticles : FEffectBase
{
    FCameraParticles();
    unsigned char Start(FEffectArch* Arch) override;
    float GetWorldPos(FVector* Out) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override { return 1; }
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
    unsigned char BuildGeometry(FSpaceCamera* Cam) override { return 1; }
    float Timer;                   // +0x6c  ctor: 0.4f + 0.4f (FLD 0x005d9a74 / FADD ST0,ST0)
    unsigned char bStartOpaque;    // +0x70  ctor 1; Start: distance factor <= 0.0f
    unsigned char Pad71[3];
    void* Camera;                  // +0x74  ctor 0; Start: FUN_0040f130 (the listener camera)
};
static_assert(sizeof(FCameraParticles) == 0x78, "CameraParticles 0x78 (LAB_004f6e40)");
static_assert(offsetof(FCameraParticles, Timer) == 0x6c, "Timer @ +0x6c");
static_assert(offsetof(FCameraParticles, bStartOpaque) == 0x70, "bStartOpaque @ +0x70");
static_assert(offsetof(FCameraParticles, Camera) == 0x74, "Camera @ +0x74");

// FUN_004f8d00 (thiscall, no args) -- the CameraParticles constructor.
void CameraParticlesConstruct(FCameraParticles* This);

// LAB_004f6e40 (slot 0 of the factory DAT_00674c5c, .rdata 005d99b8) -- FUN_004f79a0(0x78),
// which is malloc for any size but 0x84, then a tail jump into FUN_004f8d00; null memory returns
// null. The factory slot is the generic Effect* slot the one-shot creators read through the
// shared head only (+0x10, +0x4c, +0x58, vtable slots 0 and 8), so the object goes out as the
// slot's FParticleEffect* type; nothing reads a ParticleEffect-only field through it.
FEffectBase* CameraParticlesCreate();

// FUN_00503330 — companion alive? (particle Flags08 bit1 clear / sound / light).
unsigned char CreateEffectCompanionAlive(void* CompanionLocal);

// FUN_00503070 — createeffect one-shot. Particle + Source3D sound arms;
// light arm pending gamelit. ListOwner non-null → FUN_004fcea0(ListOwner+0x28, companion).
// Retail arg order after factory this: EffectArch, ListOwner, EngineIndex, Xform, CompanionOut.
// This wrapper is FUN_00503070 on DAT_00674c60, the factory almost every caller passes.
unsigned char CliCreateEffectOneShot(FEffectArch* EffectArch, void* ListOwner,
                                     int EngineIndex, FTransform const* LocalXform,
                                     void* CompanionOut);

struct FCliEffectFactory;
// FUN_00503070 with its `this` factory explicit (slots 0/1/2 through it). Callers on another
// factory: 0x005148a6 (FUN_00514750, direct, DAT_006751cc) and 0x005140a7 (FUN_00514070 forwards
// its own ECX: DAT_006751cc from 0x00514448 and 0x00516792, DAT_00674c60 from the others).
unsigned char CliCreateEffectOneShotOn(FCliEffectFactory const* Factory, FEffectArch* EffectArch,
                                       void* ListOwner, int EngineIndex,
                                       FTransform const* LocalXform, void* CompanionOut);

// The effect factories FUN_00503f40 / FUN_00503070 are thiscall on: .data objects holding only
// a vptr, stamped by static initializers, whose three slots make the particle (+0), the sound
// (+4) and the light (+8):
//   DAT_00674c60 (LAB_004f6e20) -> .rdata 005d99a8 {LAB_004f6e70, LAB_00503420, LAB_00513480}
//   DAT_00674c5c (LAB_004f6e30) -> .rdata 005d99b8 {LAB_004f6e40, LAB_00513460, LAB_00513480}
//   DAT_00674c58 (LAB_004f6e60) -> .rdata 005d99c8 {LAB_004f6e70, LAB_004b5010, LAB_00513480}
//   DAT_00674fcc (LAB_00503390) -> .rdata 005daab4 {LAB_005033a0, LAB_00503420, LAB_00513480}
//   DAT_006751cc (LAB_00513430) -> .rdata 005db760 {LAB_00513440, LAB_00513460, LAB_00513480}
// All but DAT_00674c58 have a table here; that one's only XREF is its own static init (0x004f6e60,
// W) -- nothing loads it as a factory (its sound slot LAB_004b5010 is `XOR EAX,EAX / RET`). The particle slot is the
// generic Effect* slot: the one-shot creators touch the object only through the shared head
// and vtable slots 0 and 8, so a CameraParticles goes through it as well.
struct ISoundSource;
struct FCliEffectFactory
{
    virtual FEffectBase* CreateParticle() const = 0;
    virtual ISoundSource* CreateSound() const = 0;
    virtual FGameLight* CreateLight() const = 0;
};

struct FStandardCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FJumpCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FCameraCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FTunnelCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

extern FStandardCliEffectFactory const G_CliEffectFactoryC60;
extern FJumpCliEffectFactory const G_CliEffectFactoryJump;
extern FCameraCliEffectFactory const G_CliEffectFactoryCameraParticles;
extern FTunnelCliEffectFactory const G_CliEffectFactoryTunnel;

// FUN_00503f40 (thiscall on Factory, RET 0x10) — world-space one-shot (no ListOwner attach).
// Copies 0x30 LocalXform into particle+0x10 / light+0xf0 (or EngineLightSetTransform when
// light EngIndex==-1); Pos3 → particle+0x4c / sound SetVelocity / GameLight+0x120.
// CompanionOut may be null (tradelane player leave/approach). No FUN_004fcea0.
unsigned char CliCreateEffectWorldOneShotOn(FCliEffectFactory const* Factory,
                                            FEffectArch* EffectArch, FTransform const* LocalXform,
                                            float const* Pos3, void* CompanionOut);

// FUN_00503f40 on DAT_00674c60 (the explosion's factory, 0x005364b1..0x005365a7; also 0x00514bde,
// 0x0052bdfc, 0x0052e57c, 0x0053208b). The DAT_00674c5c callers (0x00514ad1, 0x00514e70,
// 0x0051513b, 0x0052b3c9) go through CliCreateEffectWorldOneShotOn with
// G_CliEffectFactoryCameraParticles.
unsigned char CliCreateEffectWorldOneShot(FEffectArch* EffectArch, FTransform const* LocalXform,
                                          float const* Pos3, void* CompanionOut);
