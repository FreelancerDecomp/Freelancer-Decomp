// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "beam_effect.h"
#include "client_equip_fx.h" // FClientEquip
#include "vis_effect_arch.h"
#include "x86math.h"

#include "common_cequip.h"
#include "common_csys.h"
#include "common_cobject.h"
#include "common_id.h"
#include "engine_light.h"

#include <cstddef>

class FClientGameobj;     // client_gameobj.h
class FClientBeamGameobj; // client_gameobj.h -- retail Beam (0xd4, FUN_0052dde0 alloc)
struct FParticleEffect;   // particle_effect.h
struct ISoundSource;      // soundmanager.h

// CliLauncher fire host — retail CliLauncher.cpp (FUN_0052c830 / FUN_0052d880).
// A ClientEquip (client_equip_fx.h): Equip +0x04 is the CELauncher, Host +0x08 the ship's
// gameobj, SeparationExplosion +0x0c. sizeof 0x40 for the launcher / mine / CM (operator
// new(0x40) at 0x005331ea / 0x005332e6; FUN_0052c830 zeroes through +0x3c) and 0x44 for the
// gun (operator new(0x44) at 0x005333ea; its +0x40 lives in FCliGunFx). Retail never builds a
// plain launcher: FUN_0052c830's only callers are the mine / CM / gun constructors.
//
// Retail vtables (six slots each; slots 3..5 are the fire hooks TryFire / FireBarrelAim /
// FireDirect):
//   launcher PTR_LAB_005dd4bc {LAB_0052ccb0, FUN_0052ca80, FUN_0052cd40, FUN_0052d410,
//                              FUN_0052d280, FUN_0052d510}
//   mine     PTR_FUN_005dd564 {FUN_0052d5c0, FUN_0052d590, FUN_0052cd40, ...same 3..5}
//   CM       PTR_FUN_005dd598 {FUN_0052d700, FUN_0052d590, FUN_0052cd40, ...same 3..5}
//   gun      PTR_LAB_005dd5d8 {LAB_0052da00, FUN_0052d590, FUN_0052dbf0, FUN_0052dd00,
//                              FUN_0052d280, FUN_0052dd40}
// (FUN_0052d590 is the scalar deleting destructor over the same body FUN_0052caa0.)
//
// FUN_0052c830 / FUN_0052caa0's unwind maps (FuncInfo 005fa4dc / 005fa510) name two member
// objects with destructors after the ClientEquip base (FUN_00531af0): the +0x10 use-animation
// handle (FUN_0052ca50) and the +0x30 effect companion (FUN_00502d90). Constructed in that
// order, destroyed in reverse after the class body.

// The +0x10 use-animation playback handle, -1 for none.
struct FLauncherAnimHandle
{
    int Value; // IAnimation PlayScript handle
    // FUN_0052ca50: a handle other than -1 -> IAnimation vtable +0x38 (StopPlayback) and +0x28
    // (DestroyScript) on it, then -1. No AnimDB::Rem (unlike the class body's release).
    ~FLauncherAnimHandle();
};

// The +0x30 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*}.
struct FLauncherEffectCompanion
{
    void* Slots[4];
    // FUN_00502d90 (CliEffectCompanionClear).
    ~FLauncherEffectCompanion();
};

struct FCliLauncherFx : FClientEquip
{
    // The +0x04 equipment as the launcher it always is here (FUN_0052c830's argument).
    CELauncher* LaunchEquip() const { return static_cast<CELauncher*>(Equip); }

    // Fields as FUN_0052c830 fills them and FUN_0052caa0 releases them.
    FLauncherAnimHandle AnimJointIndex;        // +0x10  use_animation PlayScript handle, -1 none
    void* DryFireSoundArch;                    // +0x14  launcher companion +0x80, Acquired
    void* OneShotSoundArch;                    // +0x18  munition companion +0x5c, Acquired
    ISoundSource* FireVoice;                   // +0x1c  made on first shot by FUN_0052ced0
    FEffectArch* ConstEffectArch;              // +0x20  munition companion +0x58, AddRef'd
    Archetype::Explosion* MunitionExplosion;   // +0x24  munition explosion_arch, AddRef'd
    void* MuzzleEffect;                        // +0x28  live particle (PlayMuzzleFx)
    FEffectArch* DefaultEffectArch;            // +0x2c  "multi_sparks02" (DAT_00675448)
    // +0x30 the 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*} the
    // Update fills with "multi_sparks02" (FUN_00503070, 0x0052ce6b).
    FLauncherEffectCompanion Companion;

    FCliLauncherFx();
    // FUN_0052caa0 (the body of the scalar deleting FUN_0052ca80 / FUN_0052d590).
    ~FCliLauncherFx() override;

    // LAB_0052ccb0 (RET 8): code 0xa -- the use-animation dropped, the companion out of the
    // host's effect list, the muzzle effect killed and released -- then FUN_00531b20.
    void notify(Event Code, void* Param) override;
    // FUN_0052cd40 (RET 4).
    bool Update(float Dt) override;

    // The three fire slots every launcher-family table adds after Update (see the tables
    // above). Retail names are unknown; these names say what each does.
    // Slot 3 -- FUN_0052d410 (RET 4): CELauncher::Fire (vtable +0x90) at Target; FireOk plays
    // the muzzle, consumes, updates the player's ammo and battle music (true); the refusals
    // play the dry-fire sound or notify the player's ship (false). Gun: FUN_0052dd00.
    virtual unsigned char TryFire(Vector const* Target);
    // Slot 4 -- FUN_0052d280 (no args): aim 1e10 along the averaged barrel direction, slot 3
    // on it, and on success IServer::FireWeapon (DAT_0067ecd0 slot 0). Shared by every table.
    virtual unsigned char FireBarrelAim();
    // Slot 5 -- FUN_0052d510 (RET 4, Target unread): with an equipment, the muzzle, the
    // multiplayer resource consumption and the battle-music note. Gun: FUN_0052dd40.
    virtual void FireDirect(Vector const* Target);

    // FUN_0052cf80 (RET 4, returns through a hidden ID_String): the one-shot sound's linked
    // archetype (slot 12) nickname id (+0x04), or a cleared id -- the key FUN_00534870 groups
    // the launchers that fired together by.
    ID_String OneShotSoundId() const;
    // FUN_0052ced0 (RET 8): with a one-shot sound (+0x18), the fire voice (+0x1c, made on first
    // use) goes to Pos, attenuated by the archetype's attenuation times (ShotCount - 1),
    // negated, pitched by its random pitch, and started (idle) or rewound (playing).
    void PlayFireSound(Vector const* Pos, int ShotCount);

protected:
    // The code-0xa body LAB_0052ccb0 shares with the mine / CM / gun notifies (each inlines it
    // after its own player voice line): 0x0052ccbd..0x0052cd1c.
    void DropOnDestroying();
};
static_assert(sizeof(FCliLauncherFx) == 0x40, "CliLauncherFx 0x40");
static_assert(offsetof(FCliLauncherFx, AnimJointIndex) == 0x10, "AnimJoint @ +0x10");
static_assert(offsetof(FCliLauncherFx, FireVoice) == 0x1c, "FireVoice @ +0x1c");
static_assert(offsetof(FCliLauncherFx, MuzzleEffect) == 0x28, "MuzzleEffect @ +0x28");
static_assert(offsetof(FCliLauncherFx, DefaultEffectArch) == 0x2c, "sparks arch @ +0x2c");
static_assert(offsetof(FCliLauncherFx, Companion) == 0x30, "companion @ +0x30");

// FUN_0052d570: the mine dropper -- the launcher with vtable PTR_FUN_005dd564.
struct FCliMineDropperFx : FCliLauncherFx
{
    // FUN_0052d5c0 (RET 8).
    void notify(Event Code, void* Param) override;
};

// FUN_0052d6e0: the countermeasure dropper -- vtable PTR_FUN_005dd598.
struct FCliCmDropperFx : FCliLauncherFx
{
    // FUN_0052d700 (RET 8).
    void notify(Event Code, void* Param) override;
};

// FUN_0052d880: the gun -- vtable PTR_LAB_005dd5d8, +0x40 its turret-motion voice.
struct FCliGunFx : FCliLauncherFx
{
    // LAB_0052da00 (RET 8).
    void notify(Event Code, void* Param) override;
    // FUN_0052dbf0 (RET 4).
    bool Update(float Dt) override;
    // Slot 3 -- FUN_0052dd00 (RET 4): FUN_0052d410 (non-virtual), then an unguided munition
    // (Seeker +0x90 == 0) launches its projectiles (FUN_0052dde0).
    unsigned char TryFire(Vector const* Target) override;
    // Slot 5 -- FUN_0052dd40 (RET 4): FUN_0052d510's body, then the target point for an owned
    // host and the projectiles of an unguided munition.
    void FireDirect(Vector const* Target) override;

    // +0x40 the looping turret-motion voice (FUN_0052d880). No destructor of the gun releases
    // it: the gun's scalar deleting destructor FUN_0052d590 runs only FUN_0052caa0.
    ISoundSource* TurretVoice;
};
static_assert(sizeof(FCliGunFx) == 0x44, "CliGunFx 0x44");

// FUN_0052d570 — MineDropper (base + Mine vtable stamp).
FCliLauncherFx* CliMineEquipFxConstruct(CEquip* Equip, void* Host);
// FUN_0052d6e0 — CmDropper.
FCliLauncherFx* CliCmEquipFxConstruct(CEquip* Equip, void* Host);
// FUN_0052d880 — Gun arm (TurretVoice Source3D via CreateClientSoundSource3D).
FCliLauncherFx* CliGunEquipFxConstruct(CEquip* Equip, void* Host);

// Gun archetype Companion (ICliObj) — muzzle fields used by FUN_0052cfd0.
// Retail Gun::ReadUnknown: flash_particle_name@+0x54, flash_radius@+0x58,
// light_anim name@+0x60 (char buf) with length@+0x5c (0 = no anim).
struct FCliGunFxCompanion
{
    unsigned char Pad00[0x54];
    ID_String OneShotEffect; // +0x54  flash_particle_name
    float FlashRadius;       // +0x58  FUN_00537af0 Range
    unsigned LightAnimLen;   // +0x5c  0 → skip player-light arm
    char LightAnimName[32];  // +0x60  FUN_00538e80 (NOT ID_String)
};
static_assert(offsetof(FCliGunFxCompanion, OneShotEffect) == 0x54, "gun fx @ +0x54");
static_assert(offsetof(FCliGunFxCompanion, FlashRadius) == 0x58, "flash radius @ +0x58");
static_assert(offsetof(FCliGunFxCompanion, LightAnimLen) == 0x5c, "anim len @ +0x5c");
static_assert(offsetof(FCliGunFxCompanion, LightAnimName) == 0x60, "anim name @ +0x60");

class FClientEquippedObj; // client_gameobj.h

// Client equip-update packet (10 dwords) — FUN_00534460 / FUN_00544d70 / FUN_00544e70.
struct FCliEquipFirePacket
{
    int Op;                      // +0x00  0x18 fire / 0x19 activate / 0x1a|0x1b→0x19 / 0x1c / 0xbb
    unsigned char Opaque04[0xc];
    unsigned short HardpointId;  // +0x10  0 → null ids / count 0
    unsigned short Pad12;
    int ActivateSubOp;           // +0x14  0x19 cases 0..4; rewritten by FUN_00544e70 to 3
    Vector TargetPos;            // +0x18  also 0x1c payload dword pair with +0x14
    unsigned char Pad24[4];      // +0x24  tenth dword of the 10-dword copy
};
static_assert(offsetof(FCliEquipFirePacket, HardpointId) == 0x10, "fire pkt hp @ +0x10");
static_assert(offsetof(FCliEquipFirePacket, ActivateSubOp) == 0x14, "activate sub @ +0x14");
static_assert(offsetof(FCliEquipFirePacket, TargetPos) == 0x18, "fire pkt tgt @ +0x18");
static_assert(sizeof(FCliEquipFirePacket) == 0x28, "fire pkt 0x28 (10 dwords)");

// FUN_004fc8b0
void BeamEffectSetProgressFromSpeed(FBeamEffect* This, float Speed);
// FUN_004fc8f0 (thiscall, RET 8) -- Flags70 bit 1 = bHit, bit 2 = bHitPlayer (the other bits
// kept). Caller: Beam slot 101 LAB_0053f690 (0x0053f738) with (1, Other == player).
void BeamEffectSetHitFlags(FBeamEffect* This, bool bHit, bool bHitPlayer);

// FUN_005154a0 (thiscall on the 0x10 companion, RET 0x10) -- +0x00 Arch, +0x04 Sound,
// +0x08 Particle, +0x0c Light (retail argument order: Arch, Particle, Sound, Light), AddRef
// each non-null one. Callers: Beam activate FUN_0053f340 (Beam +0xc0) and the effect paths.
void CliEffectCompanionInit(void* Companion16, void* Arch, FEffectBase* Particle,
                            ISoundSource* Sound, FGameLight* Light);

// FUN_004fcea0 — insert payload (0x10) into intrusive list at ListOwner+0x28.
// Payload layout matches FUN_004fd480: [0] arch/slot0, [1] sound, [2] particle/beam, [3] light.
void CliEffectListInsert(void* ListOwnerPlus28, void const* Payload16);

// FUN_004fcef0 — find matching payload in list at ListOwner+0x28, ArmDeath/stop/release,
// clear companion (FUN_00502d90), return 1 if found.
unsigned char CliEffectListRemove(void* ListOwnerPlus28, void* Payload16);

// FUN_00502d90 — release particle(+8)/sound(+4)/light(+0xc) and zero 0x10 companion.
void CliEffectCompanionClear(void* Companion16);

// The inline stop sequence that precedes a companion clear (FUN_004fcef0, FUN_005139d0,
// FUN_005137c0, FUN_00503a80): particle vt+4 (ArmDeath), sound vt+0x94(-1, identity pose) and
// vt+0x60 (Stop), light FUN_005379b0 then vt+8 (disable).
void CliEffectCompanionArmStop(void* Companion16);

// FUN_004fd0d0 (thiscall on the list at ListOwner+0x28, RET 4) — stop and release every
// node's companion (or, with a non-null ArchFilter, only the nodes whose slot 0 is non-null and
// holds ArchFilter in its first dword), then erase EVERY node. Callers: GameObject's dtor
// (0x00539565, 0x0053961e), the list's own dtor FUN_005393e0 (0x00539407) and slot 108
// (0x0053a892).
void CliEffectListClear(void* ListOwnerPlus28, void* ArchFilter);

// FUN_004fd230 (thiscall on the list at ListOwner+0x28, RET 8) — every node whose engine
// instance is PartIndex or a descendant leaves the list: with NewOwner, a still-alive one is
// moved to the end of NewOwner's list (FUN_004fd420 + FUN_004f7d80); the rest are stopped and
// released. Callers: EquippedObj notify 5/6/7/8 (0x005347c7, 0x00534842), FUN_00533d10.
void CliEffectListDetachPart(void* ListOwnerPlus28, long PartIndex, FClientGameobj* NewOwner);

// FUN_004fd480 — copy 4 pointers + AddRef particle(+8) / sound(+4) / light(+0xc).
void CliEffectListCopyPayload(void* DstSlots, void const* SrcSlots);

// FUN_005377a0 — copy velocity/offset float[3] into GameLight+0x120.
void GameLightSetFreeMotionOffset(FGameLight* Light, float const* Offset3);

// FUN_005377c0 — explosion FX local transform (when SlotExplosionFx non-null).
void CliExplosionAttachLocalXform(void* ExplosionFx, int EngineIndex, FTransform const* LocalXform);

// FUN_0053f340 (thiscall on the Beam, RET 8, non-virtual) -- the Beam's activate: beam effect
// + light into the +0xc0 companion, insert it into the effect list, then Projectile slot 92
// FUN_005426e0 called directly. Caller: CliLauncher fire FUN_0052dde0.
void CliBeamActivateEffects(FClientBeamGameobj* This, Vector const* LookDir,
                            unsigned char bUseLookDir);

// FUN_0052dde0 — CliLauncher fire → new Beam → create/init → activate → register.
void CliLauncherFireProjectiles(FCliLauncherFx* Launcher, Vector const* Target);

// FUN_0052cfd0 — muzzle: AnimDB / player gamelit / ParticleEffect(004f8060+004f8160).
void CliLauncherPlayMuzzleFx(FCliLauncherFx* Launcher);

// The fire hooks FUN_0052d410 / FUN_0052d280 / FUN_0052d510 / FUN_0052dd00 / FUN_0052dd40 are
// the FCliLauncherFx / FCliGunFx virtuals TryFire / FireBarrelAim / FireDirect above.

// FUN_00534870 — the EquippedObj IObjAffect slot 13 (+0x34) fire dispatcher; AdjustorThis is
// the host's IObjAffect sub-object (Host+4). Local shots take the wrappers' slot 3 / 4 and send
// IServer::FireWeapon, another player's take slot 5 (multiplayer: sub-target re-aim and the
// target-relative point turned into world space); then shared one-shot sounds play once.
// Returns 0.
unsigned CliShipFireWeapons(void* AdjustorThis, unsigned short const* HardpointIds,
                            unsigned HardpointCount, Vector const* TargetPos, int PlayerId);

// LAB_00535330 — EquippedObj adjustor vt+0x4c ActivateEquip(id*, bOn, playerId). Returns
// retail's EAX: 0 on every path (0x00535459).
int CliShipActivateEquip(void* AdjustorThis, unsigned short const* HardpointId,
                         unsigned char bOn, int PlayerId);

// FUN_00534460 — EquippedObj message: 0x18 fire, 0x19 ActivateEquip cases 0..4.
// Returns 1 if handled (0x18), 0 otherwise (retail 0x19 leaves local_29=0).
unsigned char EquippedObjHandleEquipMsg(FClientEquippedObj* Host,
                                        FCliEquipFirePacket const* Packet);

// FUN_00544e70 — preprocess: op 0x1a/0x1b → 0x19 + next launcher hardpoint (subop 3).
void CliShipPreprocessEquipUpdate(FClientEquippedObj* Host, FCliEquipFirePacket* Packet);

// FUN_00544d70 — CShip equip-update: gate + copy + preprocess + EquippedObjHandleEquipMsg.
// 0x1c → FindById + Host+4 vt+0x48 (LAB_005468e0) + FUN_005351d0 walk; 0xbb → LeadAimFire.
unsigned char CliShipHandleEquipUpdate(FClientEquippedObj* Host,
                                       FCliEquipFirePacket const* Packet);

// FUN_00544880 — lead-aim → Host+4 vt+0x34 (CliShipFireWeapons).
void CliShipLeadAimFire(FClientEquippedObj* Host);

// LAB_005468e0 — CShip IObjAffect vt+0x48 set_target(target, sub, player), RET 0xc.
// AdjustorThis = Host+4.
int CliShipSetTarget(void* AdjustorThis, IObjRW* Target, unsigned short SubId, int PlayerId);

// FUN_005351d0 — EquippedObj IObjAffect vt+0x44 (hp*, target, sub, player), RET 0x10.
// AdjustorThis = Host+4.
int CliEquippedObjSetGunTarget(void* AdjustorThis, unsigned short const* HardpointId,
                               IObjRW* Target, unsigned short SubId, int PlayerId);

// FUN_005416c0 — ClientObjDb_FindObject (client_objdb.h).
// FUN_005351d0 — per-gun SetTarget + IServer::SetTarget (wired in 0x1c walk).
// LAB_005468e0 — CShip Host+4 vt+0x48 set_target/sub + IServer::SetTarget.
