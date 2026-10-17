// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client equipment-FX — retail EquippedObj.cpp factory + FlashLight.cpp light arm.
// See docs/re/equip_fx_factory.md.

#include "common_cequip.h" // INotify, CEquip, Archetype::Explosion

class FClientGameobj;

// Retail ClientEquip: the base of every equip-FX wrapper on EquippedObj +0xb8 (vtable
// PTR_FUN_005de13c). The slot order is retail's -- 0 notify (FUN_00531b20), 1 scalar deleting
// destructor (FUN_00531ab0, body FUN_00531af0), 2 Update (FUN_00532000) -- which is what the
// equipped object's update FUN_00534010 calls: slot 2 with Dt, and slot 1 with 1 when that
// returns false. Every derived wrapper's own table keeps the same three slots.
struct FClientEquip : INotify
{
    FClientEquip();
    // FUN_00531af0: vtable back to 005de13c, Equip non-null -> CEquip::NotifyDisconnecting(this),
    // SeparationExplosion non-null -> FUN_00536170.
    virtual ~FClientEquip();
    // FUN_00532000 (RET 4): `return Equip != nullptr`. Overridden by CliEngine (FUN_00528020),
    // CliLauncher / MineDropper / CounterMeasureDropper (FUN_0052cd40), CliGun (FUN_0052dbf0),
    // CliAttachedFX (FUN_00532490) and CliTradeLaneEquip (FUN_00516ea0).
    virtual bool Update(float Dt);
    // FUN_00531b20 (RET 8): code 0xa -> CEquip::NotifyDisconnecting(this) (no test), Equip = 0.
    void notify(Event Code, void* Param) override;

    FClientEquip(FClientEquip const&) = delete;
    FClientEquip& operator=(FClientEquip const&) = delete;

    CEquip* Equip;                              // +0x04
    FClientGameobj* Host;                       // +0x08
    Archetype::Explosion* SeparationExplosion;  // +0x0c
};

// FUN_00531a30 (thiscall, RET 8) -- the ClientEquip constructor body every type constructor
// runs first (called by FUN_0052c830 at 0x0052c85c, inlined into most others): +4 / +8 / +0xc,
// CEquip::ConnectClientEquip, the attached equipment's separation explosion.
void ClientEquipBaseFill(FClientEquip* Wrapper, CEquip* Equip, FClientGameobj* Host);

// One node of the EquippedObj +0xb8 list (operator new(0xc), FUN_0042a7e0): a VC6 std::list
// node whose sentinel is at +0xb8 and whose size is at +0xbc.
struct FEquipFxListNode
{
    FEquipFxListNode* Next;   // +0x00
    FEquipFxListNode* Prev;   // +0x04
    FClientEquip* Wrapper;    // +0x08
};

// FUN_00532170 (cdecl) -- IFLAppearance from DALib::Engine (QueryInterface), slot 5 (+0x14) with
// Key when the query succeeded, released either way; answers whether slot 5 succeeded.
// Callers: the cloak notify 0x00532037, the equipped object's LOD pick 0x00533847; the cloak
// destructor FUN_00531f30 inlines the same body.
bool RemoveAppearanceInstanceValue(int Key);

// FUN_00533100 (thiscall on the EquippedObj, RET 4) -- the equipment EquipId of the host's
// CEqObj (narrowed by 0x103, CEquipManager::FindByID with no test of the object), then the
// first wrapper on the +0xb8 list whose Equip is it; null when none is.
FClientEquip* EquippedObjFindEquipFx(FClientGameobj* Host, unsigned short EquipId);

// FUN_0052f9a0 (thiscall on the CliTractor wrapper, RET 8) -- the ITEMTRACTORED handler's
// (FUN_0053c5d0, 0x0053c603) second step. Wrapper must be the tractor's (FUN_00533100 with the
// tractor's equipment id). An intact tractor whose arm holds LootId drops it
// (CETractor::RemoveTarget) unless the loot keeps more than Units units; outside the player's
// own host that happens only in multiplayer.
void CliTractorEquipFxDropLoot(FClientEquip* Wrapper, unsigned LootId, int Units);

// The shield wrapper's own slots +0x14 (LAB_0052e760, RET 0xc) and +0x18 (FUN_0052e8c0, RET 8),
// virtual calls on Wrapper. Wrapper must be a shield's (the equipped object's hit slots
// FUN_005340b0 / FUN_00534190 pick it with FUN_00533100 and require CEquip type 0x100).
// HitInfo is the hit point (+0) and the surface normal (+0xc).
// Collision: the hit effect by Other's mass * COLLISION_DAMAGE_FACTOR * closing speed; Unused
// is not read.
void CliShieldEquipFxCollisionHit(FClientEquip* Wrapper, void const* HitInfo,
                                  FClientGameobj* Other, void const* Unused);
// Munition: the hit effect by hull_damage * ShieldEquipConsts::HULL_DAMAGE_FACTOR +
// energy_damage, then the munition's splash effect.
void CliShieldEquipFxMunitionHit(FClientEquip* Wrapper, void const* HitInfo,
                                 FClientGameobj* Munition);

// FUN_00534240 (thiscall on the EquippedObj, RET 0x14) -- the body of
// FClientEquippedObj::Slot1c0 (vtable +0x1c0): the first fuse named FuseId on the burning list
// (+0xc4) gets Fuse::IgniteAt(SourceId, SubObjId, AtT); otherwise the ClientFuseDB prototype is
// copied onto the list, bound to Host, randomised, given OverrideLifetime unless it is -1 and
// ignited. False (FDUMP "Fuse(%s) not found in FuseDB") only when the prototype is missing.
// Callers go through the virtual slot (a ship overrides it with FUN_00545a90).
bool HostIgniteFuse(FClientGameobj* Host, unsigned SourceId, ID_String const& FuseId,
                    unsigned short SubObjId, float AtT, float OverrideLifetime);

// FUN_00533080 — destroy every wrapper in Host+0xb8 and free list nodes.
void EquippedObjClearEquipFx(FClientGameobj* Host);

// FUN_00533160 — find equip by id, dispatch EEquipType, insert wrapper at Host+0xb8.
// All 14 retail type arms allocate; some type-ctors remain simplified (named gaps).
void EquippedObjCreateEquipFx(FClientGameobj* Host, unsigned short EquipId);

// FUN_00533e10 — clear + Traverse all equip → CreateEquipFx + fuse threshold walks
// + ResolveExplosion. Host vt+0x1c0 Ignite = FUN_00534240 via HostIgniteFuse.
void EquippedObjRebuildEquipFx(FClientGameobj* Host);

// The CliTradeLaneEquip wrapper (FUN_00516c30, EEquipType 0x800000) as the solar's lane switch
// FUN_00516910 uses it. Wrapper must be that type (FUN_00516910 picks it with
// CETradeLaneEquip::cast on Wrapper->Equip).
// +0x30 -- ships using the lane, stepped +/-1 by FUN_00516910.
int* CliTradeLaneEquipFxShipCount(FClientEquip* Wrapper);
// FUN_00516d10 -- the lane / shield one-shot effects on the ring's hardpoints.
void CliTradeLaneEquipFxAttachLaneEffects(FClientEquip* Wrapper);

// The CliEquipLight flares have no entry point of their own: the lights run through the effect
// type bucket 0x2e (FUN_004fd040 sweep + FUN_004f7810 walk), as in retail.
