// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// freelancer.exe explosion.cpp -- retail E:\FL\Scratch\Source\Client\space\explosion.cpp,
// .text:005361e0-00537258: the Action_Explode IAction a Gameobj's action list runs while its
// [Explosion] archetype plays out (effects on their delays, the innards debris, the death, the
// shatter into Debris gameobjs), plus the impulse and camera flash it applies when it starts.
//
//   Action_Explode : IAction   sizeof 0x28 (IAction::operator new(0x28) at 0x005361fc)
//   ??_7Action_Explode@@6B@ .rdata:005de584 = {LAB_005b7314 (IAction::get_id thunk),
//                                               FUN_00536460 update, FUN_00536430 dtor}
//
// ExplosionArchAddRef / ExplosionArchRelease (FUN_00536100 / FUN_00536170, the same file) live
// in client_gameobj.cpp.

#include "common_archetype.h"
#include "common_cequip.h" // IAction
#include "common_csys.h"   // Vector

#include <vector>

class FClientGameobj;
struct CObject;
struct FDebrisArch;

class Action_Explode : public IAction
{
public:
    // FUN_005361e0 (cdecl): IAction::operator new(0x28), the fields, the lifetime roll, the
    // impulse on the player and on every Gameobj of ObjDB kinds 10 and 9, the damage flash on
    // the player's HUD, then update(0). Callers: GameObject slot 108 (0x0053a8c3), Debris
    // slot 108 (0x005305dd), EquippedObj FUN_00533d10 (0x00533dbb).
    static Action_Explode* Create(Archetype::Explosion* Arch, FClientGameobj* Gameobj,
                                  Vector const* Pos);

    // FUN_00536460 (RET 4). False once the explosion is over (the action list drops it).
    bool update(float Dt) override;
    // FUN_00536430: IAction's destructor, then IAction::operator delete when bit 0 is set.
    ~Action_Explode() override;

private:
    // 0x00536219..0x00536298, the constructor part of FUN_005361e0.
    Action_Explode(Archetype::Explosion* InArch, FClientGameobj* InGameobj, Vector const& InPos);

    // The two tick tests FUN_00536460 spells out inline (see the .cpp): the lifetime ran out
    // during this tick; a delay (>= 0: from the start; < 0 or NaN: the lifetime running out)
    // was reached during this tick.
    bool LifetimeEndedThisTick() const;
    bool DelayReachedThisTick(float Delay) const;

    // FUN_005366a0 (RET 4): push Target's CSimple away from Pos when its centre of mass is
    // inside the explosion radius.
    void ApplyImpulse(FClientGameobj* Target);
    // FUN_00536800: split the Gameobj's engine instance tree into pieces and fly each one off
    // as a Debris gameobj (process = shatter).
    void Shatter();
    // FUN_00536c20 (RET 0xc): collect the pieces of Instance's subtree, at most MaxPieces.
    void CollectChildPieces(long Instance, int MaxPieces, std::vector<CObject*>& Pieces);
    // FUN_00536fb0: throw innards_debris_num Debris gameobjs out of the Gameobj.
    void SpawnInnardsDebris();

    Archetype::Explosion* Arch;   // +0x08
    FClientGameobj* Gameobj;      // +0x0c
    Vector Pos;                   // +0x10
    float Lifetime;               // +0x1c  lerp(Arch->Lifetime, Arch->LifetimeMax, rand)
    float Elapsed;                // +0x20  -1.0f until the first update
    float PrevElapsed;            // +0x24  -1.0f until the second update
};

// FUN_00536b50 (cdecl) -- a debris_type drawn by weight (rand()/32768 against the running sum
// of the counts), looked up in the [Debris] table; the default record (FUN_00530a10) when the
// draw falls past the end or the name is not in the table (FDUMP warning). Callers: the
// shatter (0x0053688f) and the innards (0x005370bc).
FDebrisArch* PickExplosionDebrisArch(std::vector<Archetype::FExplosionDebrisType> const* Types);
