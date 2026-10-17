// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ===========================================================================
// Server.dll's read/write object wrappers: GameObject / EqObj / Ship / Solar /
// Loot.
//
// These are NOT the simulated objects. The simulated objects are Common.dll's
// CObject family (CShip, CSolar, CLoot, ...). What Server.dll owns is a thin
// wrapper that implements IObjRW over one of them, and that wrapper is what the
// whole pub::SpaceObj:: export family hands around: every one of those exports
// resolves a FLID to one of these, then dispatches through its vtable.
//
// The hierarchy is RTTI, not inference. Server.dll.txt's ClassHierarchy-
// Descriptors spell it out:
//   Loot  base array .rdata:06d6d0a8 = { Loot, GameObject, IObjInspectImpl,
//                                        IObjRW, IObjInspect, IObjAffect,
//                                        Watchable, CBase }
//   Ship  base array .rdata:06d6cd38 = { Ship, EqObj, GameObject,
//                                        IObjInspectImpl, ... }
//   Solar base array .rdata:06d6cdc4 = { Solar, EqObj, GameObject,
//                                        IObjInspectImpl, ... }
// i.e. GameObject : IObjInspectImpl, EqObj : GameObject, Ship/Solar : EqObj,
// Loot : GameObject. IObjInspectImpl is Common.dll's (common_iface.h) and
// already implements all 85 IObjInspect slots by delegating to the CObject it
// holds in its CBase sub-object at +0x10 -- which is exactly why retail can get
// away with such a thin wrapper.
//
// The constructors confirm the same layout byte for byte:
//   GameObject::GameObject  .text:06cee360  -- IObjRW + IObjInspectImpl inlined
//                                              (+0x00/+0x04 interface vtables,
//                                              +0x08 Watchable, +0x0c CBase
//                                              vtable, +0x10 CObject*), then its
//                                              own members +0x14..+0x5f, then
//                                              its own three vtables.
//   EqObj::EqObj            .text:06ce6620  -- GameObject then +0x60..+0x8b.
//   Ship::Ship              .text:06d00e80  -- operator new(200 == 0xc8), EqObj
//                                              then +0x90..+0xc7.
//
// VTABLES (re-checked slot by slot 2026-09-25, claude-code:g-vt1):
//   * The extension vtable is declared in retail slot ORDER: GameObject +0x154 .. +0x1b8
//     (111 slots) and EqObj +0x1bc .. +0x24c (148 slots), each with its retail offset and
//     base body next to it. The absolute slot numbers are retail's too: IObjInspect's 85
//     slots come first, and neither IObjInspectImpl's nor GameObject's virtual destructor
//     adds a primary slot -- it overrides CBase's (the +0x0c sub-object's only slot), the
//     same place retail keeps it (.rdata:06d65764 -> FUN_06cee470).
//   * IObjAffect (the +0x04 sub-object): GameObject's own table (.rdata:06d6576c) is a row
//     of retail error stubs and is ported as such; EqObj's six overrides and Ship's own are
//     ported below; Solar keeps EqObj's.
// ===========================================================================

#include <list>
#include <vector>

#include "common_iface.h"
#include "common_cobject.h"
#include "common_archetype.h"
// EquipDescList (Ship::Jettison), EquipDesc. DamageList (common_misc.h)
// for get_damage_list below; common_cobject.h already pulls it in, but say so.
#include "common_goods.h"
#include "common_misc.h"
#include "server_exports.h"

class StarSystem;   // server_sysdb.h -- retail GameObject+0x28 points at one
class GameObject;
class ServerEquip;  // Server.dll's per-equipment listener (EqObj+0x70 list); defined below EqObj
// ServerFuse (Fuse + 8 bytes, vtable .rdata:06d65724) is held BY VALUE in
// EqObj's +0x7c list, so its full definition is needed here.
#include "server_fuse.h"

// ---------------------------------------------------------------------------
// Argument records of the GameObject/EqObj extension slots. Retail passes each
// of these by pointer; the layouts are read off the base bodies that consume
// them and the call sites that build them (cited per field). Server-internal:
// none of them crosses a DLL boundary.
// ---------------------------------------------------------------------------

// +0x194 set_invincible's command, 0x0c bytes -- the node value of the
// GameObject+0x2c list (node 0x14 = 8 + 0x0c). .text:06cef0f0 reads +0 (the key,
// compared per node at .text:06cef118), +4/+5 (bytes, copied into +0x38/+0x39)
// and +8 (float, into +0x3c, reduced with a minimum at .text:06cef14e).
// Ship FUN_06d01110 (.text:06d01143) passes {Type 1, bProtect 1, bFromPlayers 1,
// MaxHpLoss 0.0f} (.text:06d0112c / 06d01130 / 06d01134 / 06d0113b).
struct FInvincibility
{
    unsigned int Type;          // +0x00  list key
    unsigned char bProtect;     // +0x04  blocks non-player damage   -> GameObject+0x38
    unsigned char bFromPlayers; // +0x05  blocks player damage too    -> GameObject+0x39
    unsigned char Pad06[2];
    float MaxHpLoss;            // +0x08  fraction of max hp that may still be lost -> +0x3c
};
static_assert(sizeof(FInvincibility) == 0x0c, "FInvincibility is the 0x0c list value");

// +0x17c damage_sub_obj's record. Built by StarSystem FUN_06d0d730
// (.text:06d0d7ad..06d0d7ca): +0 = the inflictor's id, +4 = the damaged
// sub-object, +8 = the damage, +0xc = FUN_06d0d730's own third (byte) argument.
// Read by .text:06ceeb04 (+4) / .text:06ceeb23 (+8) and EqObj .text:06ce926c /
// .text:06ce92bc.
struct FSubObjDamage
{
    unsigned int InflictorId;   // +0x00
    unsigned short SubObjId;    // +0x04
    unsigned short Pad06;
    float Damage;               // +0x08
    unsigned char Field_0x0c;   // +0x0c  a byte flag; meaning not yet derived
    unsigned char Pad0d[3];
};

// +0x180 munition_hit's record. .text:06ceeb44 reads the sub-object at +8 and
// .text:06ceeb5f/06ceeb66 the munition archetype at +4 (its +0x80 HullDamage).
struct FMunitionImpact
{
    unsigned int ShooterId;                  // +0x00
    Archetype::Munition const* Munition;     // +0x04
    unsigned short SubObjId;                 // +0x08
    unsigned short Pad0a;
    Vector HitPos;                           // +0x0c
};

// +0x184 explosion_hit's record (and +0x1fc..+0x208). .text:06ceeb80 reads the
// position at +0xc and the Explosion archetype at +0x18 (Radius +4, HullDamage
// +0xc); EqObj's bodies read the inflictor at +4 and the cause at +8.
struct ExplosionDamageEvent
{
    unsigned int ExplodingObjId;             // +0x00
    unsigned int InflictorId;                // +0x04
    DamageCause Cause;                       // +0x08
    Vector Position;                         // +0x0c
    Archetype::Explosion const* Explosion;   // +0x18
    // +0x1c -- a collision-group name the blast skips. StarSystem's explosion
    // (FUN_06d0b260) reads it per candidate: .text:06d0b36a `MOV ECX,[ESP+0x188]`
    // (the record) / .text:06d0b371 `MOV ECX,[ECX+0x1c]`, and when both it and
    // PhySys::GetCollisionGroup(candidate) are non-null and DACOM stricmp says
    // equal (.text:06d0b381), the candidate is not collected. The MedMine blast
    // stores 0 here (.text:06d04eaa `MOV [ESP+0x40],EDI`, record base S+0x24).
    char const* SkipCollisionGroup;          // +0x1c
};

// +0x1a4 on_collision's record, 0x30 bytes, built twice per PhySys collision by
// the system collision pass (.text:06ce35a2..06ce3610 for side A,
// .text:06ce3616..06ce36cc for side B): each side gets its own sub-object, the
// other side's wrapper and sub-object, the shared contact point, and the two
// vectors at +0x18/+0x24 NEGATED for side B (FCHS at .text:06ce3618..06ce367b).
struct FCollisionEvent
{
    unsigned short SubObjId;                 // +0x00
    unsigned short Pad02;
    GameObject* Other;                       // +0x04
    unsigned short OtherSubObjId;            // +0x08
    unsigned short Pad0a;
    Vector Point;                            // +0x0c  same for both sides
    Vector Field_0x18;                       // +0x18  negated for side B
    Vector Field_0x24;                       // +0x24  negated for side B
};
static_assert(sizeof(FCollisionEvent) == 0x30, "FCollisionEvent 0x30");

// One loot object to create, 0x6c bytes -- the element of the std::vector that
// EqObj::build_loot (+0x244, .text:06cebe80) / build_equip_loot (+0x248,
// .text:06cec260) fill and StarSystem FUN_06d08a40 consumes (the callers divide
// the byte span by 0x6c: `MOV EAX,0x4bda12f7 / IMUL / SAR EDX,5`).
// Field sources: .text:06cec284..06cec4b7 and .text:06cebeb0..06cec219.
struct FLootRecord
{
    unsigned int Id;                                 // +0x00  CFLIDMaker id & 0x3fffffff
    Archetype::Equipment const* Arch;                // +0x04  the loot object's archetype
    float HitPts;                                    // +0x08
    Archetype::Equipment const* ContentsArch;        // +0x0c  what it carries (itself unless a cargo pod)
    float ContentsHitPts;                            // +0x10
    int Count;                                       // +0x14
    unsigned int Field_0x18;                         // +0x18  zeroed by build_loot; meaning not derived
    unsigned int Field_0x1c;                         // +0x1c  zeroed by build_loot; meaning not derived
    Vector Velocity;                                 // +0x20
    Vector Position;                                 // +0x2c
    Matrix Orientation;                              // +0x38  (9 dwords, copied with REP MOVSD ECX=9)
    Vector AngularVelocity;                          // +0x5c
    unsigned char Field_0x68;                        // +0x68  build_loot stores 1 (.text:06cebecf)
    unsigned char bMission;                          // +0x69  EquipDesc+0x18 / CEquip vt+0x14
    unsigned char Pad6a[2];
};
static_assert(sizeof(FLootRecord) == 0x6c, "FLootRecord is the 0x6c-byte loot element");

// Controller message 0x39 (FUN_06d43650, the port's Server_ControllerDispatch
// broadcast): "this object was destroyed". Built on the stack by EqObj::destroy
// (.text:06ce8291-06ce8339) and Loot::destroy (.text:06cf2684-06cf26fc); the
// dispatcher only forwards the pointer.
struct FObjectDestroyedEvent
{
    unsigned int ObjId;                      // +0x00  get_id()
    unsigned int KillerId;                   // +0x04  destroy()'s second argument
    unsigned int SystemId;                   // +0x08  FUN_06d06cd0(OwnerSystem)
    unsigned int OwnerPlayer;                // +0x0c  EqObj: CSimple+0xb4; Loot: 0
    int Kind;                                // +0x10  destroy()'s first argument
    Vector Position;                         // +0x14  get_position()
    // +0x20 (VC6 vector: allocator byte, first/last/end). EqObj fills it through
    // collect_droppable_equip (+0x1f4) when Kind == 1; Loot leaves it empty.
    std::vector<EquipDesc> DroppedEquip;
};

// Controller message 0xd (FUN_06d43650): "a sub-object's hit points were
// applied". Built on the stack by EqObj's four apply_*_entry slots
// (root .text:06ceb576..06ceb603, equip .text:06ceb895..06ceb940, power
// .text:06cebd66..06cebdf3, group .text:06cebae5..06cebb9b): nine dwords
// zeroed, then filled per field below.
struct FSubObjDamageEvent
{
    unsigned int ObjId;                      // +0x00  get_id()
    unsigned int SystemId;                   // +0x04  FUN_06d06cd0(OwnerSystem)
    unsigned int InflictorId;                // +0x08  DamageList+0x14
    unsigned int EquipType;                  // +0x0c  CEquip+0x14 (equip), 0x40000 (power), 0 otherwise
    unsigned int CollisionGroupType;         // +0x10  CArchGroup::GetCollisionGroupType (group), 0 otherwise
    DamageCause Cause;                       // +0x14  DamageList+0x10
    float Damage;                            // +0x18  hit points before - Entry.HitPts
    float RelativeDamage;                    // +0x1c  Damage / max hit points
    float RelativeHealth;                    // +0x20  Entry.HitPts / max hit points
};
static_assert(sizeof(FSubObjDamageEvent) == 0x24, "FSubObjDamageEvent is nine dwords");

// Controller message 0x13 (FUN_06d43650): "this equipment is about to be
// dropped as loot". Built by EqObj::destroy_equip (.text:06cea0b2..06cea109)
// and EqObj::destroy_cargo (.text:06ce9d55..06ce9da2) for fate 4 only: the four
// dwords, then CEquip vt+0x28 GetEquipDesc into Desc. After the dispatch retail
// copies Desc back out (REP MOVSD, 8 dwords) and loots THAT copy, so a handler
// may rewrite it.
struct FEquipLootEvent
{
    unsigned int SystemId;                   // +0x00
    unsigned int InflictorId;                // +0x04
    unsigned int ObjId;                      // +0x08
    DamageCause Cause;                       // +0x0c
    EquipDesc Desc;                          // +0x10
};
static_assert(sizeof(FEquipLootEvent) == 0x30, "FEquipLootEvent is 0x30 bytes");

// Controller message 0x38 (FUN_06d43650), raised the first time a death fuse is
// lit: by EqObj::light_fuse (.text:06cec62c..06cec66a) and EqObj::destroy
// (.text:06ce80e4..06ce812a). The handler may write a lifetime into +0x0c; the
// caller reads it back after the dispatch.
struct FDeathFuseEvent
{
    unsigned int SystemId;                   // +0x00
    unsigned int ObjId;                      // +0x04
    unsigned int ObjType;                    // +0x08  IObjInspect slot 34 get_type
    float Lifetime;                          // +0x0c  zeroed before the dispatch
};
static_assert(sizeof(FDeathFuseEvent) == 0x10, "FDeathFuseEvent is four dwords");

// ---------------------------------------------------------------------------
// GameObject -- the base wrapper. .text:06cee360.
// ---------------------------------------------------------------------------
class GameObject : public IObjInspectImpl
{
public:
    GameObject();
    virtual ~GameObject();

    // -- Server.dll's own extension of the IObjInspect vtable ---------------
    //
    // DECLARATION ORDER IS RETAIL SLOT ORDER (2026-09-24, claude-code:w5-server-06ce).
    // Every virtual below is declared in the order of GameObject's own vtable
    // (.rdata:06d65244 family; GameObject ends at +0x1b8 = 111 slots, EqObj
    // continues to +0x24c = 148 slots, Solar adds +0x250). The byte offset in each
    // `// +0xNNN` comment is the retail slot; the address is the base body. The
    // absolute slot numbers match retail's as well (IObjInspect's 85 first; the
    // virtual destructor overrides CBase's slot and adds none here -- see the file
    // header). Do not insert a virtual between two of these without a retail slot
    // for it.

    // -- The four slots retail puts between IObjInspect's last (+0x150) and
    //    create() (+0x164). -------------------------------------------------
    //
    // IDENTIFIED 2026-09-17 (Server.dll lane, wave 8). The header comment above
    // called these "four unidentified GameObject slots"; they are not
    // unidentified any more. Ship's vtable is at .rdata:06d6711c, anchored
    // twice and independently: create() sits at +0x164 (.rdata:06d67280 ->
    // .text:06d01040, this file's own citation) and get_invincible_flag() at
    // +0x198 (.rdata:06d672b4 -> .text:06ce61a0, likewise). Slot +0x150 is
    // .text:06d5fd02, `JMP dword ptr [->COMMON.DLL::IObjInspectImpl::...]`,
    // i.e. an import thunk -- which is exactly what IObjInspect's last slot
    // (cobject(), implemented in Common.dll) has to look like from here, and is
    // the third confirmation of the base address.
    //
    // All thirteen Server.dll vtables that share this layout were dumped and
    // compared, which is what separates GameObject's own body from an override:
    //   +0x154  base .text:06cef1c0;  overridden 5x (CounterMeasure .text:06ce61f0,
    //           .text:06cf3400, .text:06cf3d70, .text:06d08eb0, .text:06d09b90)
    //   +0x158  base .text:06cee630;  overridden 6x (EqObj .text:06ce8080, ...)
    //   +0x15c  base .text:06cee670;  overridden by EqObj/Ship/Solar
    //           (.text:06ce7c80, which tail-jumps back into the base body)
    //   +0x160  .text:06cee6d0 in ALL THIRTEEN -- never overridden.

    // +0x154. Base .text:06cef1c0:
    //     MOV EAX,[ECX+0x10] / <the standard (ObjectClass & 3) == 3 guard> /
    //     MOV EAX,[EAX+0xe0] / RET
    // i.e. `return cobj()->TypeBits;` -- CSimple+0xe0, which common_cobject.h
    // already names TypeBits ("arch ObjectType after open"). Distinct from
    // IObjInspect::get_type(uint&) at slot 34.
    virtual unsigned int get_type_bits() const;

    // +0x158. Base .text:06cee630, RET 8 -- two arguments, always returns true:
    //     cobj()->set_hit_pts(0.0f);        ; PUSH 0 / CALL [CObjectVt + 0x98]
    //     StarSystem_OnObjectDestroyed(OwnerSystem, this, Kind);   ; 06d0b160
    //     this->destroy_notify();           ; CALL [own vtable + 0x15c]
    //     return true;
    // The CObject vtable offsets are read off CSimple's own table
    // (??_7CSimple@@6B@ at .rdata:0639b884 in Common.dll): +0x98 is
    // .rdata:0639b91c == CSimple::set_hit_pts, +0x94 is .rdata:0639b918 ==
    // CSimple::disconnect. Only the FIRST argument reaches the base body;
    // retail compares it against 1, both inside .text:06d0b160 (06d0b1ea
    // `CMP EBP,0x1`) and in EqObj's override (06ce80d8 `CMP EDI,0x1`). The
    // second argument is the object's own id: StarSystem::remove_player builds
    // it with `CALL [EDI+0x20]` (IObjInspect::get_id) and pushes it second
    // (.text:06d087cf..06d087d8).
    virtual bool destroy(int Kind, unsigned int ObjId);

    // +0x15c. Base .text:06cee670, RET 0:
    //     .text:06cee673  c6464001  MOV byte ptr [ESI+0x40],0x1   ; bDestroyed
    //     if (SinglePlayer() && cobj()->OwnerPlayer != 0) return;  ; CSimple+0xb4
    //     cobj()->disconnect(OwnerSystem);          ; CALL [CObjectVt + 0x94]
    // EqObj/Ship/Solar override it (.text:06ce7c80) to first drain the
    // equipment list at EqObj+0x64 through .text:06d0c730 and then
    // `JMP FUN_06cee670` straight into this base body.
    virtual void destroy_notify();

    // +0x160. `MOV AL,[ECX+0x40]; RET` at .text:06cee6d0. The predicate
    // StarSystem::remove_player tests before destroying an object
    // (.text:06d087c0 `CALL [EAX+0x160]` / `TEST AL,AL` / skip when set).
    virtual bool is_destroyed() const;

    // Retail slot +0x164 (Ship .text:06d01040, Solar .text:06d02c50, Loot
    // .text:06cf2440, CounterMeasure .text:06ce6200): COMMON.DLL::CObject::Alloc
    // with the subclass's literal ObjectClass into CBase::Object,
    // Archetype::Root::create_instance(Object), DALib::Engine vtable +0x6c,
    // CObject::open(Arch) and CBase::advise(true). The ORDER differs per
    // subclass (Ship advises before open, Solar opens first, Loot opens first and
    // creates the instance last) and so does the result (Ship/Solar:
    // Object != nullptr; Loot/CounterMeasure: get_index() != -1).
    // GameObject's and EqObj's own entries are _purecall (.text:06d5fff0).
    virtual bool create(Archetype::Root* Arch) = 0;

    // Retail slot +0x168 (Ship .text:06d010a0): COMMON.DLL::CObject::Find(Id,
    // ObjectClass) instead of Alloc, then the same CBase::advise(true).
    // _purecall in GameObject/EqObj like +0x164.
    virtual bool find(unsigned int const& Id) = 0;

    // Retail slot +0x16c (Ship .rdata:06d67288 -> .text:06d01450). Called from
    // FUN_06cf4f40 during StarSystem physics. Base body is FUN_06cee9e0:
    // return !is_destroyed(). EqObj overrides with .text:06ce7e20, Ship with
    // LAB_06d01450.
    virtual bool update(float Dt);

    // +0x170 -- base .text:06cee6e0 (RET 8); Ship overrides with .text:06d01a60.
    // OwnerSystem = Sys; when Parms is given, CObject::remake_physical(*Parms,
    // 0.0f) (CObject vt +0x78); outside single player an owned object starts
    // remote-controlled physics (PhySys::RCBegin); then CSimple::connect(Sys)
    // (vt +0x90) unless it is a player's object in single player.
    virtual void enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms);

    // +0x174 -- base .text:06cee810 (RET 4). The inverse of enter_system: the
    // system is told first (FUN_06d0cfa0 -> +0x178 on every object), RCEnd,
    // then OutParms (when given) is filled from the live physics body, the body
    // is unmade (CObject vt +0x74), CSimple::disconnect, OwnerSystem = nullptr.
    virtual void leave_system(PhySys::CreateParms* OutParms);

    // +0x178 -- base .text:06cee980 (RET 4). Called by StarSystem FUN_06d0cfa0 on
    // every object of the system when ObjId leaves it. A CEqObj forwards a
    // type-3 notification record to its IBehaviorManager (vt slot 0).
    virtual void on_object_left_system(unsigned int ObjId);

    // +0x17c -- base .text:06ceeb00 (RET 8); EqObj .text:06ce9250, Solar
    // .text:06d02d40. Hull part: damage_hull(Hit.Damage) when Hit.SubObjId is
    // SUBOBJ_ID_ROOT or SUBOBJ_ID_NONE.
    virtual void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage);

    // +0x180 -- base .text:06ceeb40 (RET 8); EqObj .text:06ce9350, Solar
    // .text:06d02d90. Hull part: damage_hull(Munition::HullDamage) for a hit on
    // the root / no sub-object.
    virtual void munition_hit(FMunitionImpact const& Hit, DamageList& Damage);

    // +0x184 -- base .text:06ceeb80 (RET 8); EqObj .text:06ce9500. Hull damage
    // from an explosion, in three distance bands of the blast radius.
    virtual void explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage);

    // +0x188 -- base .text:06ceee70 (RET 4); EqObj/Solar .text:06ceb330, Ship
    // .text:06d01a10. Applies a finished DamageList: root entries through
    // apply_root_damage (+0x1b4), the system broadcast (StarSystem_DamageObject,
    // FUN_06d0b800), and destroy(1, InflictorId) when the list says destroyed.
    virtual void apply_damage_list(DamageList& Damage);

    // +0x18c -- .text:06ce6190 `MOV AL,[ECX+0x41]; RET`. Never overridden.
    virtual bool is_in_damage_callback() const;

    // +0x190 -- .text:06ceefa0 (RET 4). Sets the +0x41 latch; clearing it
    // flushes whatever PendingDamage collected meanwhile through +0x188.
    virtual void set_in_damage_callback(bool bIn);

    // +0x194 -- .text:06cef0f0 (RET 4). Adds or replaces one invincibility
    // command (keyed by Type) in the +0x2c list and recomputes +0x38/+0x39/+0x3c.
    virtual void set_invincible(FInvincibility const& Cmd);

    // Retail slot +0x198 -- `MOV AL,[ECX+0x39]; RET` at .text:06ce61a0.
    virtual unsigned char get_invincible_flag() const;
    // Retail slot +0x19c -- `MOV AL,[ECX+0x38]; RET` at .text:06ce61b0.
    virtual unsigned char is_invincible() const;
    // Retail slot +0x1a0 -- `FLD [ECX+0x3c]; RET` at .text:06ce61c0.
    virtual float get_invincible_timer() const;

    // +0x1a4 -- .text:06cee9f0 (RET 4). Object-vs-object collision: relative
    // speed -> Collision::CalcCollisionDamage, handed to the system
    // (StarSystem FUN_06d0d730), which damages the OTHER object.
    virtual void on_collision(FCollisionEvent const& Hit);

    // Retail slot +0x1a8. ADDED 2026-09-17 (wave 14) because the per-frame
    // observer update needs it: .text:06cf61dd is
    //   .text:06cf61be  6800000040      PUSH 0x40000000
    //   .text:06cf61c3  8d442470        LEA  EAX,[ESP + 0x70]
    //   .text:06cf61c7  50              PUSH EAX
    //   .text:06cf61dd  ff92a8010000    CALL dword ptr [EDX + 0x1a8]
    // on a stack DamageList that the caller then tests for emptiness.
    //
    // CORRECTION, and the reason the second argument is a float here: 0x40000000
    // is NOT a bitmask. Ship's override for this slot is .rdata:06d672c4 ->
    // .text:06ceb210 (Ship's vtable base is .rdata:06d6711c, anchored by
    // create() at +0x164 -> .text:06d01040 and get_invincible_timer() at
    // +0x1a0 -> .text:06ce61c0), and that body uses the argument as
    //   .text:06ceb295  ff5244          CALL dword ptr [EDX + 0x44]  ; CEquip status
    //   .text:06ceb298  d85c2434        FCOMP float ptr [ESP + 0x34] ; <- the arg
    //   .text:06ceb29e  f6c405          TEST  AH,0x5
    //   .text:06ceb2a1  7a9d            JP    LAB_06ceb240           ; >= -> skip
    // i.e. a FLOAT comparison against each equip's status. 0x40000000 read as a
    // float is 2.0f, so "everything, whatever its status" -- a health ceiling,
    // not a mask. The tracker's earlier note on FUN_06cf53e0 called it "mask
    // 0x40000000"; that reading is wrong and this comment supersedes it.
    //
    // PORTED 2026-09-17 (wave 15). The base body is FUN_06ceee20
    // (.text:06ceee20), which ten of the thirteen wrapper vtables carry
    // unchanged; EqObj/Ship/Solar override it with .text:06ceb210. Both are in
    // src/Server/server_gameobject.cpp with their byte trails.
    //
    // CORRECTION to the sketch that used to stand here: it said the non-
    // destroyed arm records "the equip's own status", i.e. the +0x44 value the
    // comparison uses. It does not. .text:06ceb295 `ff5244` (GetRelativeHealth)
    // is the COMPARISON and .text:06ceb2ad `ff503c` (GetHitPoints) is the value
    // that goes into the entry -- two different accessors, named by
    // ??_7CETractor@@6B@ at .rdata:0639d908 and .rdata:0639d900.
    virtual void get_damage_list(DamageList& Out, float StatusCeiling) const;

    // +0x1ac -- .text:06cef1e0 (RET 4). Queues a root entry of
    // get_max_hit_pts() * Relative in PendingDamage (destroyed when <= 0) and,
    // unless already inside a damage callback, applies it at once.
    virtual void set_relative_health(float Relative);

    // +0x1b0 -- base .text:06ceec90 (RET 8); EqObj/Ship/Solar .text:06cea3a0
    // (difficulty scale and armor first). Adds the new root hit points to
    // Damage, honouring the invincibility floor; true when the hull reaches 0.
    virtual bool damage_hull(float Amount, DamageList& Damage);

    // +0x1b4 -- .text:06ceef70 (RET 4): csimple()->set_hit_pts(Entry.HitPts).
    virtual void apply_root_damage(DamageEntry const& Entry);

    // +0x1b8 -- .text:06cef0b0 (RET 4). The invincibility gate: causes >= 0x10
    // are never blocked; otherwise a player inflictor is checked against +0x39,
    // everything else against +0x38.
    virtual bool is_damage_blocked(DamageList const& Damage) const;

    // Bind an already-existing CObject to this wrapper. Retail does exactly this in
    // its find() slot (.text:06d010a0) after COMMON.DLL::CObject::Find hands one back:
    // store it in CBase::Object, then CBase::advise(true). Named separately here
    // because the callers that need it in this port already hold the CObject (the
    // single-player system loader walks CObject::FindFirst) rather than an id.
    void attach(CObject* Obj);

    // The CObject this wrapper reads and writes, with retail's own guard: every
    // accessor in the family loads CBase::Object and nulls it unless
    // (ObjectClass & 3) == 3 (see .text:06cef1c0, .text:06cf9069 and a dozen
    // others). Non-virtual on purpose -- retail inlines the guard everywhere.
    CObject* cobj() const;
    CSimple* csimple() const;

    // -----------------------------------------------------------------
    // retail +0x14 .. +0x1F -- the "marked by" player list. DECODED
    // 2026-09-17 (wave 13); it used to be one of the "four VC6 list heads
    // this port does not model".
    //
    // The constructor builds it (raw bytes, .text:06cee360):
    //   .text:06cee3ba  6a0c     PUSH 0xc                 ; node size
    //   .text:06cee3c0  884e14   MOV  byte [ESI+0x14],CL  ; std::allocator
    //   .text:06cee3c3           CALL MSVCRT.DLL::operator_new
    //   .text:06cee3c8  8900     MOV  [EAX],EAX
    //   .text:06cee3ca  894004   MOV  [EAX+4],EAX         ; self-linked sentinel
    //   .text:06cee3cd  894618   MOV  [ESI+0x18],EAX      ; _Head
    //   .text:06cee3d0  895e1c   MOV  [ESI+0x1c],EBX      ; _Size = 0
    // so the MSVC6 shape is {allocator +0x14, _Head +0x18, _Size +0x1C} over
    // 0x0C-byte nodes {next, prev, unsigned int} -- and the element really is
    // a bare dword, because FUN_06cef3b0's own `operator new(0xc)` writes only
    // node[2].
    //
    // What the dword IS comes from retail's own export name:
    // pub::Player::MarkObj (?MarkObj@Player@pub@@YAHIIH@Z, .text:06d549d5)
    // resolves the object id and calls FUN_06cef3b0(obj, player_id, mark_type)
    // -- so the list holds PLAYER IDS, and the predicate FUN_06cef450 answers
    // "has that player marked this object?".
    //
    // FUN_06cef3b0 (.text:06cef3b0) is mark_for_player() below: it searches the
    // list for the id, push_backs it when MarkType == 1 and it is absent,
    // erases it when MarkType == 0 and it is present, and keeps _Size in step
    // both ways.
    // FUN_06cef450 (.text:06cef450) is is_marked_by(): fourteen instructions,
    // `MOV ECX,[ECX+0x18]` / `CMP [EAX+8],EDX` / `SETNZ AL`.
    //
    // The answer is a wire field in at least three payloads:
    //   .text:06cf6c85  884208        MOV byte [EDX + 0x8],AL    ; CREATELOOT +0x08
    //   .text:06d0addd  888424dc000000 MOV byte [ESP + 0xdc],AL  ; CREATESHIP +0xA8
    //   .text:06d0a63b  8885a8000000  MOV byte [EBP + 0xa8],AL   ; CREATESOLAR +0xA8
    // The CREATESOLAR site is the cleanest of the three and is worth reading in
    // full, because the two dwords it writes are the two payload fields this
    // wave chased: .text:06d0a630 loads the observer's player id off node+0x40,
    // .text:06d0a636 asks this predicate, .text:06d0a63b stores the answer,
    // .text:06d0a64f is `MOV EAX,[EDX + EAX*1 + 0xffffff00]` (Players +
    // id*0x418 - 0x100, i.e. PlayerData::rep_vibe), .text:06d0a65b stores THAT
    // at payload +0xb0, and .text:06d0a661 finally calls the notifier.
    //
    // A real container, because retail has one. The port uses std::list rather
    // than reproducing the intrusive node by hand: nothing here is a raw heap
    // block the way Observer's node is, so there is no reason to.
    std::list<unsigned int> MarkedByPlayers;

    // FUN_06cef3b0 (.text:06cef3b0, `RET 0x8`). MarkType is retail's `param_2`,
    // and retail tests it against the literals 1 and 0 rather than for
    // non-zero: `.text:06cef3d0  83fa01  CMP EDX,0x1` adds,
    // `.text:06cef41b  85d2  TEST EDX,EDX` removes, and every other value falls
    // through to LAB_06cef442 having done nothing at all. The port keeps the int
    // and keeps that three-way split -- do not collapse it to a bool.
    void mark_for_player(unsigned int PlayerId, int MarkType);

    // FUN_06cef450 (.text:06cef450, `RET 0x4`).
    bool is_marked_by(unsigned int PlayerId) const;

    // GameObject's own data members, from the constructor at .text:06cee360
    // (every member it writes, 2026-09-24) and the accessors above.

    // retail +0x20/+0x24 -- a DOUBLE. The ctor zeroes both dwords
    // (.text:06cee3d7/06cee3da); the only writer is EqObj::apply_damage_list,
    // `CALL [->Timing::read]` / `FSTP double ptr [ESI+0x20]` (.text:06ceb3cb /
    // .text:06ceb3d1) while the hull's relative health is above 0; readers are
    // EqObj::update (.text:06ce7ef7 `FSUB double ptr [EBX+0x20]`) and StarSystem
    // (.text:06d0bc27).
    double LastDamageTime;

    // retail +0x2c/+0x30/+0x34 -- VC6 std::list of 0x14-byte nodes (0x0c of
    // value): the active invincibility commands, one per Type. Built by the ctor
    // (`PUSH 0x14` / operator new), maintained only by set_invincible (+0x194),
    // freed by the dtor through FUN_06d0f620.
    std::list<FInvincibility> Invincibilities;

    // +0x38/+0x39/+0x3c are the reduction of Invincibilities that
    // set_invincible (+0x194) recomputes: "any command blocks non-player damage",
    // "any command blocks player damage", and the minimum over the commands of
    // the fraction of max hit points the object may still LOSE (the floor is
    // 1 - that fraction, see damage_hull). The historical name InvincibleTimer
    // is kept because other lanes' code spells it; it is NOT a timer.
    unsigned char bInvincible;      // retail +0x38, ctor zeroes it
    unsigned char bInvincibleFlag;  // retail +0x39, ctor zeroes it
    float InvincibleTimer;          // retail +0x3c, ctor stores 1.0f -- max hp-loss fraction

    // retail +0x40. Written only by destroy_notify() (.text:06cee673) and read
    // only by is_destroyed() (.text:06cee6d0); a one-way latch.
    unsigned char bDestroyed;

    // retail +0x41 -- read by is_in_damage_callback (+0x18c), written by
    // set_in_damage_callback (+0x190).
    unsigned char bInDamageCallback;

    // retail +0x44..+0x5f -- a DamageList by value (0x1c bytes). The ctor
    // constructs it and then stores 0x18 into its Cause (+0x54, .text:06cee452 `MOV dword ptr [ESI+0x54],0x18`);
    // set_relative_health (+0x1ac) queues into it and set_in_damage_callback
    // (+0x190) drains it.
    DamageList PendingDamage;

    // retail +0x28. The StarSystem this object lives in. Two independent reads
    // prove what it is:
    //   * .text:06d0b160 takes it in ECX and immediately walks `[ECX+0xc]` to
    //     `[ECX+0xc]`'s sentinel -- StarSystem's player list (server_sysdb.h).
    //   * .text:06cee6b3 hands it to CSimple::disconnect, whose parameter is an
    //     IObjDB*. Retail's StarSystem is an IObjDB (RTTI .?AVStarSystem@@,
    //     single inheritance), and so is the port's (server_sysdb.h);
    //     destroy_notify() makes that call.
    StarSystem* OwnerSystem;

    // -- IObjAffect: GameObject's own table (.rdata:06d6576c), every entry a retail error
    //    stub (-3 / -8 / 0, see server_gameobject.cpp). Ship and EqObj override most of
    //    them (EqObj's six below, Ship's in its own class). ----------------------------
    int set_throttle(float Throttle) override;
    int set_axis_throttle(Vector const& AxisThrottle) override;
    int set_nudge_vec(Vector const& NudgeVec) override;
    int set_strafe_dir(StrafeDir Dir) override;
    int set_formation_leader(IObjRW* Leader) override;
    int set_formation_offset(Vector const& Offset) override;
    int add_formation_follower(IObjRW* Follower) override;
    int remove_formation_follower(IObjRW* Follower) override;
    int get_formation_follower_count() override;
    int set_follow_leader(IObjRW* Leader) override;
    int set_follow_offset(Vector const& Offset) override;
    int add_follow_follower(IObjRW* Follower) override;
    int remove_follow_follower(IObjRW* Follower) override;
    int slot_34(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                int SourceClient) override;
    int slot_38(unsigned short EquipId, int Count, int SourceClient) override;
    int slot_3c(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                int SourceClient) override;
    int slot_40(unsigned short const* EquipId, IObjRW* Target, int SourceClient) override;
    int slot_44(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                int SourceClient) override;
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override;
    int slot_4c(unsigned short const* EquipId, bool bActivate, int SourceClient) override;
    int slot_50(bool bEngage, bool bArg2, int SourceClient) override;
    int slot_54(bool bActivate, int SourceClient) override;
    int slot_58(unsigned short EquipId, unsigned Count, int SourceClient) override;
    int slot_5c(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                int bFlag, RequestCB* Callback) override;
    int slot_60(int RequestType, unsigned const* TargetId, int Arg3) override;
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     bool bFastExit, float Speed) override;
    int stop_tradelane() override;
    int slot_6c(int ArgA, int ArgB) override;
    int slot_70(unsigned Id) override;
    int slot_74(unsigned Arg) override;

protected:
    // Ship::create's body (.text:06d01040). Retail has no such shared function;
    // Solar::create and Loot::create order the same calls differently (see
    // create() above), so they spell their own sequence out.
    bool CreateOfClass(CObject::Class ObjectClass, Archetype::Root* Arch);
    bool FindOfClass(CObject::Class ObjectClass, unsigned int const& Id);
};

// ---------------------------------------------------------------------------
// EqObj -- .text:06ce6620. Adds no modelled state (see the header comment).
// ---------------------------------------------------------------------------
class EqObj : public GameObject
{
public:
    EqObj();
    virtual ~EqObj();

    // Retail slot +0x1a8 again -- .text:06ceb210, the equipment-aware override.
    // It is installed in exactly three vtables and this class is the common
    // ancestor of all three: EqObj's own (.rdata:06d65244 + 0x1a8 ==
    // .rdata:06d653ec), Ship's (.rdata:06d6711c + 0x1a8 == .rdata:06d672c4) and
    // Solar's (.rdata:06d6746c + 0x1a8 == .rdata:06d67614). Every other wrapper
    // keeps the base FUN_06ceee20.
    //
    // NOT an ABI change: the slot already exists on GameObject, this only
    // overrides it.
    virtual void get_damage_list(DamageList& Out, float StatusCeiling) const;

    CEqObj* ceqobj() const;

    // Retail FUN_06ce7e20 (.text:06ce7e20, RET 4). GameObject::update's gate
    // (FUN_06cee9e0), the weapons-offline repair pass, the dock-bay tick
    // (FUN_06ce7930), every ServerEquip's update, the cargo-decay timer (+0x6c,
    // decay_cargo every 1.0 s), Fuse::Burn on every lit fuse; true while alive.
    bool update(float Dt) override;

    // -- GameObject slots EqObj overrides (vtable .rdata:06d65244) -----------
    // +0x38 (IObjInspect slot 14, .rdata:06d6527c; Ship 06d67154 and Solar
    // 06d674a4 keep it) .text:06ce6890: `MOV EAX,[ECX]; JMP [EAX+0x1ec]` --
    // is_death_fuse_burning.
    bool is_dying() const override;

    // -- IObjAffect overrides (EqObj's table .rdata:06d651c4; Ship .rdata:06d6709c
    //    and Solar keep these six entries). `this` is the +4 sub-object in
    //    retail; the listing's [this+N] are EqObj+N+4. ----------------------
    // slot 13 (+0x34) .text:06ce68a0 (RET 0x10): fire Count equips by id.
    int slot_34(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                int SourceClient) override;
    // slot 15 (+0x3c) .text:06ce6d80 (RET 0xc): tractor every object of Targets.
    int slot_3c(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                int SourceClient) override;
    // slot 16 (+0x40) .text:06ce6ba0 (RET 0xc): tractor one object.
    int slot_40(unsigned short const* EquipId, IObjRW* Target, int SourceClient) override;
    // slot 17 (+0x44) .text:06ce6f60 (RET 0x10): CEGun::SetTarget on one gun.
    int slot_44(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                int SourceClient) override;
    // slot 19 (+0x4c) .text:06ce70b0 (RET 0xc): (de)activate one equip.
    int slot_4c(unsigned short const* EquipId, bool bActivate, int SourceClient) override;
    // slot 22 (+0x58) .text:06ce7210 (RET 0xc): use Count of a repair kit /
    // shield battery cargo stack ("EqObj::use_item()" in its FDUMP).
    int slot_58(unsigned short EquipId, unsigned Count, int SourceClient) override;
    // FUN_06ce7350 (RET 0xc, non-virtual): slot_58's repair-kit branch; mends
    // the hull and returns how many kits that took. EquipId is not read.
    int use_repair_kits(unsigned short EquipId, Archetype::RepairKit const* Kit, int Count);
    // FUN_06ce7630 (RET 0xc, non-virtual): slot_58's shield-battery branch;
    // recharges the weakest shield (type 0x100) and returns how many
    // batteries that took. EquipId is not read.
    int use_shield_batteries(unsigned short EquipId, Archetype::ShieldBattery const* Battery,
                             int Count);
    bool destroy(int Kind, unsigned int ObjId) override;                 // +0x158 .text:06ce8080
    void destroy_notify() override;                                      // +0x15c .text:06ce7c80
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;      // +0x17c .text:06ce9250
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;      // +0x180 .text:06ce9350
    void explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage) override; // +0x184 .text:06ce9500
    void apply_damage_list(DamageList& Damage) override;                 // +0x188 .text:06ceb330
    bool damage_hull(float Amount, DamageList& Damage) override;         // +0x1b0 .text:06cea3a0

    // -- EqObj's own extension slots, +0x1bc .. +0x24c, in retail order -----
    // Retail bodies are identical in EqObj's, Ship's and Solar's tables except
    // where noted (Ship +0x1e4 .text:06d01a90 and +0x1f4 .text:06d01c90, Solar
    // +0x1fc .text:06d02f20 and its own +0x250 .text:06d03070).

    // +0x1bc .text:06ce7d00 (RET 0). rebuild_server_equip (+0x1dc), then lights
    // every archetype fuse (Archetype::EqObj +0x74 list) whose hit-point
    // threshold the hull has reached, then every arch group's FuseIgnitions.
    virtual void check_fuses();
    // +0x1c0 .text:06ce8c50 (RET 0xc). damage_hull(Damage) -- with bCanKill only
    // for Damage >= 0, without it only when the hull stays above 0.
    virtual void inflict_hull_damage(float Damage, bool bCanKill, DamageList& Out);
    // +0x1c4 .text:06ce8d40 (RET 0x10). By arch-group NAME (StringAlloc +
    // CArchGroupManager::FindByName); false when there is no such group.
    virtual bool inflict_group_damage(char const* GroupName, float Damage, bool bCanKill,
                                      DamageList& Out);
    // +0x1c8 .text:06ce8cd0 (RET 0xc). destroy_group(group, Fate, Out, true).
    virtual bool destroy_group_by_name(char const* GroupName, DamageEntry::SubObjFate Fate,
                                       DamageList& Out);
    // +0x1cc .text:06ce8e90 (RET 0x10). By HARDPOINT (CEquipManager::FindByHardpoint).
    virtual bool inflict_equip_damage(char const* Hardpoint, float Damage, bool bCanKill,
                                      DamageList& Out);
    // +0x1d0 .text:06ce8e10 (RET 0xc). destroy_equip(equip, Fate, Out).
    virtual bool destroy_equip_by_hardpoint(char const* Hardpoint, DamageEntry::SubObjFate Fate,
                                            DamageList& Out);
    // +0x1d4 .text:06ce8f50 (RET 8). Every cargo item goes out as loot through
    // build_loot (+0x244) and StarSystem FUN_06d08a40; Hardpoint (may be null)
    // becomes the loot's origin hardpoint.
    virtual bool dump_cargo(char const* Hardpoint, DamageList& Out);
    // +0x1d8 .text:06ce91e0 (RET 4). damage_shield(shield, -1.0f, Out) for every
    // shield (CEquipTraverser 0x100); always true.
    virtual bool drain_shields(DamageList& Out);
    // +0x1dc .text:06ce88d0 (RET 0). clear_server_equip, then one ServerEquip
    // (FUN_06ce8990) per CEquip.
    virtual void rebuild_server_equip();
    // +0x1e0 .text:06ce8930 (RET 0). Deletes every ServerEquip (their vtable
    // slot 1) and empties the +0x70 list.
    virtual void clear_server_equip();
    // +0x1e4 .text:06cec4d0 (RET 0x14); Ship overrides with .text:06d01a90.
    // Re-ignites the burning copy of FuseId if there is one; otherwise copies the
    // prototype out of ServerFuseDB into Fuses, binds this object as owner,
    // raises controller event 0x38 the first time a death fuse is lit, applies
    // the Lifetime override and ignites it. Broadcasts through StarSystem
    // FUN_06d0e750 when the ignition took. Returns whether a fuse was found.
    virtual bool light_fuse(unsigned int SourceId, ID_String const& FuseId,
                            unsigned short SubObjId, float AtT, float Lifetime);
    // +0x1e8 .text:06cec7f0 (RET 0xc). Fuse::UnBurn on the burning copy of FuseId,
    // broadcast through FUN_06d0e750 when it took. Returns whether it was found.
    virtual bool unlight_fuse(ID_String const& FuseId, unsigned short SubObjId, float AtT);
    // +0x1ec .text:06cec8d0 (RET 0). Any death fuse (Fuse+0xc) not yet spent.
    virtual bool is_death_fuse_burning();
    // +0x1f0 .text:06ce8400 (RET 0). The archetype's explosion_arch (Root+0x24),
    // as a cause-4 ExplosionDamageEvent through StarSystem FUN_06d0b260.
    virtual void explode();
    // +0x1f4 .text:06ce8510 (RET 4); Ship overrides with .text:06d01c90. Appends
    // the EquipDesc of every intact, lootable, non-cargo-pod equip.
    virtual void collect_droppable_equip(std::vector<EquipDesc>& Out);
    // +0x1f8 .text:06ce8760 (RET 4). At most six build_loot records' worth of
    // Descs, created as loot through StarSystem FUN_06d08a40.
    virtual void drop_loot(std::vector<EquipDesc> const& Descs);
    // +0x1fc .. +0x208: the four explosion passes explosion_hit (+0x184) runs --
    // shield first (+0x204; true == absorbed, the rest is skipped), then
    // equipment, arch groups and power.
    // +0x1fc is the HULL pass (CEqObj::get_explosion_dmg_bounding_sphere, then
    // damage_hull); +0x200 walks the attached equipment (CEquipTraverser 0x1ee0,
    // damage_equip) and then every arch group (damage_group).
    virtual void explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out);    // +0x1fc .text:06ce9550 (Solar .text:06d02f20)
    virtual void explosion_hit_subobjs(ExplosionDamageEvent const& Blast, DamageList& Out); // +0x200 .text:06ce9690
    virtual bool explosion_hit_shield(ExplosionDamageEvent const& Blast, DamageList& Out);  // +0x204 .text:06ce9a90
    virtual void explosion_hit_power(ExplosionDamageEvent const& Blast, DamageList& Out);   // +0x208 .text:06ce9940
    // +0x20c .text:06ce94b0 (RET 0xc). A munition hitting the shield bubble.
    virtual void munition_hit_shield(CEShield* Shield, Archetype::Munition const* Munition,
                                     DamageList& Out);
    // +0x210 .text:06cea4a0 (RET 0xc). Damage one equip (not a shield). Returns
    // true only when the equip was destroyed (and destroy_equip +0x224 ran):
    // `MOV AL,1` at .text:06cea734, `XOR AL,AL` on every other path (.text:06cea6d5);
    // Ship FUN_06d012e0 tests it (.text:06d013b8 TEST AL,AL).
    virtual bool damage_equip(CEquip* Equip, float Damage, DamageList& Out);
    // +0x214 .text:06cea740 (RET 0xc). Damage the shield; Damage < 0 drains it.
    // Returns true when the new hit points cross the shield's OfflineThreshold
    // (CEShield+0x74) in either direction (.text:06cea9a9 .. 06cea9d6).
    virtual bool damage_shield(CEShield* Shield, float Damage, DamageList& Out);
    // +0x218 .text:06ceaa80 (RET 0xc). Damage one arch group. Returns true only
    // when the group was destroyed (`MOV EAX,1` at .text:06ceadab; `XOR AL,AL` /
    // `XOR EAX,EAX` elsewhere); Ship FUN_06d012e0 tests it (.text:06d01425).
    virtual bool damage_group(CArchGroup* Group, float Damage, DamageList& Out);
    // +0x21c .text:06ceafc0 (RET 8). Energy (power) damage; true when the new
    // power entry is <= 0 (`MOV EAX,1` at .text:06ceb1e3).
    virtual bool damage_power(float Damage, DamageList& Out);
    // +0x220 .text:06cea9f0 (RET 0x10).
    virtual void destroy_group(CArchGroup* Group, DamageEntry::SubObjFate Fate, DamageList& Out,
                               bool bWithChildren);
    // +0x224 .text:06ce9f70 (RET 0xc). Destroys one equip; fates 4/5 (loot)
    // spawn it as loot, cargo pods (type 0x800) go through destroy_cargo.
    virtual void destroy_equip(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out);
    // +0x228 .text:06ce9c50 (RET 0xc).
    virtual void destroy_cargo(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out);
    // +0x22c .text:06ceadc0 (RET 8). CArchGroup::LinkedEquipmentType's equipment
    // takes LinkedEquipmentDamage * its max hit points (damage_equip); linked
    // SHIELDS (0x100) only FDUMP "Implement me!" (line 0x91b).
    virtual void damage_linked_equipment(CArchGroup* Group, DamageList& Out);
    // +0x230 .text:06ceaea0 (RET 8). Everything mounted inside the group dies
    // with it: external equipment (0x1fe3, not lights / attached FX) through
    // destroy_equip(fate 1), child groups through destroy_group(fate 1, false).
    virtual void destroy_group_contents(CArchGroup* Group, DamageList& Out);
    // +0x234 .. +0x240 -- apply_damage_list's per-entry dispatch (.text:06ceb3f5
    // .. 06ceb463): root, equip, power, arch group. Pushed as (Entry, List), so
    // the List is the FIRST parameter.
    virtual void apply_root_entry(DamageList const& Damage, DamageEntry const& Entry);   // +0x234 .text:06ceb550
    virtual void apply_equip_entry(DamageList const& Damage, DamageEntry const& Entry);  // +0x238 .text:06ceb7b0
    virtual void apply_power_entry(DamageList const& Damage, DamageEntry const& Entry);  // +0x23c .text:06cebd40
    // Group entries are written: retail clears Entry+8 (the fate) at .text:06cebd1a
    // while a death fuse burns and the cause is < 0x10.
    virtual void apply_group_entry(DamageList const& Damage, DamageEntry& Entry);        // +0x240 .text:06ceba40
    // +0x244 .text:06cebe80 (RET 8). Appends the loot record(s) one EquipDesc
    // turns into; returns how many.
    virtual int build_loot(EquipDesc const& Desc, std::vector<FLootRecord>& Out);
    // +0x248 .text:06cec260 (RET 8). One attached equip -> one loot record
    // (fresh FLID, position/orientation from the engine instance, explosive
    // impulse and a random spin); returns true.
    virtual bool build_equip_loot(CAttachedEquip const* Equip, FLootRecord& Out);
    // +0x24c .text:06cec910 (RET 4). Cargo decay: every cargo stack with a count
    // loses GetDecayDamagePerSecond * Elapsed, gated on the object not being a
    // solar, CEquipManager::DecayingCargoCount > 0 and the owning player having no
    // open trade (PlayerData+0x3cc). update() calls it with the +0x6c timer.
    virtual void decay_cargo(float Elapsed);

    // FUN_06ce8990 (.text:06ce8990, RET 4, non-virtual). One ServerEquip for the
    // equip with this id, by type; other types get none.
    void create_server_equip(unsigned short EquipId);
    // FUN_06ce7930 (.text:06ce7930, RET 4, non-virtual). The dock-bay tick over
    // DockQueue; update()'s only callee of its kind.
    void process_dock_queue(float Dt);
    // FUN_06ce7c50 (.text:06ce7c50, RET 4, non-virtual). True when a DockQueue
    // node carries this ship id. Sole caller IServerImpl::SPRequestInvincibility
    // (.text:06cf149b).
    bool is_ship_queued(unsigned int ShipId) const;
    // FUN_06ce7850 (.text:06ce7850, RET 8, non-virtual). ceqobj() through the
    // (& 0x103) guard -- the null it can yield is dereferenced -- tail-jumping
    // into CEqObj::activate (vt +0xb4, Common .rdata:0639bf10). Sole caller
    // StarSystem::activate_object (.text:06d0d8da).
    ObjActivateResult activate(bool bOn, unsigned int DockIndex);
    // FUN_06ce7880 (.text:06ce7880, RET 8, non-virtual). Appends {Port, ShipId}
    // to DockQueue and registers this object with its system
    // (StarSystem::add_to_list_d4, .text:06ce78c4). Sole caller
    // pub::SpaceObj::Dock (.text:06cfa891).
    void queue_dock_request(int Port, unsigned int ShipId);
    // FUN_06ce78d0 (.text:06ce78d0, RET 8, non-virtual). Erases the first
    // DockQueue node carrying ShipId whose port matches (any port when Port < 0),
    // then StarSystem::remove_from_list_d4 (.text:06ce7923); false when no node
    // matched. Sole caller pub::SpaceObj::Dock (.text:06cfa8a4).
    bool dequeue_dock_request(int Port, unsigned int ShipId);

    // -- EqObj's own members, ctor .text:06ce6620 / dtor .text:06ce6710 -----
    // +0x60/+0x64/+0x68 -- VC6 std::list of 0x10-byte nodes (8 + {int Port;
    // unsigned ShipId}): ships queued on this object's docking points. Drained
    // through StarSystem .text:06d0c730 by destroy_notify (.text:06ce7c80) and
    // cleared first thing in the dtor.
    struct FDockRequest
    {
        int Port;
        unsigned int ShipId;
    };
    std::list<FDockRequest> DockQueue;
    // +0x6c -- a FLOAT: update() adds Dt (.text:06ce8000 FLD/FADD/FST) and, past
    // 1.0f, hands it to decay_cargo and zeroes it (.text:06ce8029). Ctor zeroes it.
    float CargoDecayTimer;
    // +0x70/+0x74/+0x78 -- VC6 std::list of 0x0c-byte nodes: one ServerEquip*
    // per CEquip (rebuild_server_equip / clear_server_equip).
    std::list<ServerEquip*> ServerEquips;
    // +0x7c/+0x80/+0x84 -- VC6 std::list of 0x44-byte nodes (8 + sizeof
    // ServerFuse): the fuses lit on this object (light_fuse).
    std::list<ServerFuse> Fuses;
    // +0x88 -- set by light_fuse (.text:06cec626) the first time a fuse with
    // ServerFuse::bHasDestroyRoot is lit; gates controller event 0x38.
    unsigned char bDeathFuseLit;
};

// ---------------------------------------------------------------------------
// ServerEquip -- Server.dll's per-equipment listener (RTTI .?AUServerEquip@@,
// vtable .rdata:06d65744: notify .text:06ced360, scalar deleting dtor
// .text:06ced300, update .text:06ced350). One per CEquip of an EqObj, made by
// EqObj::create_server_equip (FUN_06ce8990) and held in EqObj+0x70. The
// constructor (.text:06ced2d0, RET 8) stores the equip at +4 and the owning
// EqObj at +8 and registers itself as the equip's server listener
// (CEquip::ConnectServerEquip). Five subclasses, each with its own vtable:
//   SrvTractor            .rdata:06d65544  new(0x0c)  notify .text:06d06a40
//   SrvRepairDroid        .rdata:06d65554  new(0x14)  update .text:06ced3f0
//   SrvThruster           .rdata:06d65564  new(0x0c)  notify .text:06ced380
//   SrvMineDropper        .rdata:06d65574  new(0x18)  slots 3..5
//   SrvCounterMeasureDropper .rdata:06d65590 new(0x18) notify + slots 3..5
//   SrvGun                .rdata:06d655ac  new(0x18)  slots 3..7
// ---------------------------------------------------------------------------
class ServerEquip : public INotify
{
public:
    // .text:06ced2d0 (RET 8): Equip at +4, Owner at +8, then
    // Equip->ConnectServerEquip(this) -- no null test on the equip.
    ServerEquip(CEquip* InEquip, EqObj* InOwner);
    // .text:06ced330 (the body behind the scalar deleting dtor .text:06ced300 and
    // .text:06ce8c20): CEquip::NotifyDisconnecting(this) while an equip is held.
    virtual ~ServerEquip();

    // slot 0 -- .text:06ced360 (RET 8). Event 0xa (the equip is being destroyed):
    // NotifyDisconnecting(this) and forget the equip.
    void notify(Event Code, void* Param) override;
    // slot 2 -- .text:06ced350 (RET 4): `Equip != 0`; Dt is not read.
    virtual bool update(float Dt);

    CEquip* Equip;        // +0x04
    EqObj* Owner;         // +0x08
};

// .rdata:06d65564 -- notify .text:06ced380 (RET 8).
class SrvThruster : public ServerEquip
{
public:
    SrvThruster(CEquip* InEquip, EqObj* InOwner);
    void notify(Event Code, void* Param) override;
};

// .rdata:06d65544 -- notify .text:06d06a40 (StarSystem lane's range, W5-C).
class SrvTractor : public ServerEquip
{
public:
    SrvTractor(CEquip* InEquip, EqObj* InOwner);
    void notify(Event Code, void* Param) override;
};

// .rdata:06d65554 -- update .text:06ced3f0 (RET 4), target pick .text:06ced6a0.
class SrvRepairDroid : public ServerEquip
{
public:
    SrvRepairDroid(CEquip* InEquip, EqObj* InOwner);
    bool update(float Dt) override;

    // FUN_06ced6a0 (RET 0, non-virtual): the most damaged attached, non-shield
    // equip or arch group of the droid's owner (strictly below 1.0 relative
    // health); SUBOBJ_ID_NONE when everything is whole.
    unsigned short pick_target() const;

    float RepairTimer;             // +0x0c  the factory stores 0 (.text:06ce8b90)
    unsigned short TargetId;       // +0x10  SUBOBJ_ID_NONE from the factory (.text:06ce8b9c)
};

// The three launcher wrappers share +0x0c/+0x10 and three more slots.
class SrvLauncher : public ServerEquip
{
public:
    SrvLauncher(CEquip* InEquip, EqObj* InOwner);

    // slot 3 (+0x0c): CELauncher::Fire(Target) must answer 9, then the shot is
    // paid for (ConsumeFireResources), counted and launched. Mine .text:06ced940,
    // countermeasure .text:06cedba0, gun .text:06cedd90.
    virtual bool fire(Vector const& Target) = 0;
    // slot 4 (+0x10): fire straight along the barrels -- slot 3 at
    // GetAvgBarrelPosWS + GetAvgBarrelDirWS * (1e10 / |dir|). Mine and
    // countermeasure share .text:06ced7a0 (the argument is not read); the gun's
    // .text:06cede30 also announces the shot to the system.
    virtual bool fire_forward(int SourceClient);
    // slot 5 (+0x14): the shot without CELauncher::Fire -- use_ammo
    // (FUN_06ced840) and, when it succeeds, the launch. Mine .text:06ced9a0,
    // countermeasure .text:06cedc00, gun .text:06cee000.
    virtual bool fire_unchecked(Vector const& Target) = 0;

    // FUN_06ced840 (RET 4, non-virtual; the Vector is not read). Refuses a
    // destroyed equip or owner; bumps FireCount; outside single player needs
    // CELauncher::AmmoNeedsMet and calls ConsumeFireResources; and when the
    // owner is a player's current ship and the projectile needs ammo, removes one
    // round from that player's cargo (PlayerData_RemoveCargo).
    bool use_ammo(Vector const& Target);

    int FireCount;                 // +0x0c  the factory stores 0
    // +0x10 -- a DOUBLE: Timing::read of the last damage, written by
    // EqObj::apply_equip_entry (.text:06ceb86c) and read by EqObj::update's
    // weapons-offline repair (.text:06ce7f72). The factory never writes it
    // (operator new garbage in retail); the port zeroes it.
    double LastDamageTime;
};

// .rdata:06d65574 (RTTI .?AUSrvMineDropper@@).
class SrvMineDropper : public SrvLauncher
{
public:
    SrvMineDropper(CEquip* InEquip, EqObj* InOwner);
    bool fire(Vector const& Target) override;
    bool fire_unchecked(Vector const& Target) override;
    // FUN_06ced9d0 (RET 4): one StarSystem::create_mine per launch record.
    void launch(Vector const& Target);
};

// .rdata:06d65590 (RTTI .?AUSrvCounterMeasureDropper@@).
class SrvCounterMeasureDropper : public SrvLauncher
{
public:
    SrvCounterMeasureDropper(CEquip* InEquip, EqObj* InOwner);
    // .text:06cedb30 (RET 8): event 0x19 on an AI-owned dropper fires it through
    // the owner's IObjAffect slot +0x34; event 0xa as the base.
    void notify(Event Code, void* Param) override;
    bool fire(Vector const& Target) override;
    bool fire_unchecked(Vector const& Target) override;
    // FUN_06cedc30 (RET 4): one StarSystem::create_counter_measure per record.
    void launch(Vector const& Target);
};

// .rdata:06d655ac (RTTI .?AUSrvGun@@), eight slots.
class SrvGun : public SrvLauncher
{
public:
    SrvGun(CEquip* InEquip, EqObj* InOwner);
    bool fire(Vector const& Target) override;
    bool fire_forward(int SourceClient) override;
    bool fire_unchecked(Vector const& Target) override;
    // slot 6 (+0x18) .text:06cee1f0 (.rdata:06d655c4): one StarSystem::create_bullet
    // per launch record. fire (.text:06cede0e) calls it for an unguided munition
    // outside single player.
    virtual void launch_bullets(Vector const& Target);
    // slot 7 (+0x1c) .text:06cee040 (.rdata:06d655c8): one StarSystem::create_guided
    // per record (with the lock target); fire / fire_unchecked call it when
    // Munition::Seeker (+0x90) is non-zero (.text:06ceddf4 / .text:06cee02a).
    virtual void launch_guided(Vector const& Target);
};

// The launch records the dropper/gun wrappers hand to StarSystem. Built on the
// stack by .text:06ceda4c (mines), .text:06cedcac (countermeasures) and
// .text:06cee0c4 (guided); read by FUN_06d08ef0 / FUN_06d092c0 / FUN_06d09640.
struct FServerLaunchRecord
{
    unsigned int OwnerPlayer;                // +0x00  owner's CSimple+0xb4
    unsigned int OwnerId;                    // +0x04  owner's get_id()
    unsigned short EquipId;                  // +0x08
    unsigned short Pad0a;
    Archetype::Equipment const* Arch;        // +0x0c  Mine / CounterMeasure / Munition
    Vector Position;                         // +0x10  ProjLaunchInfo+0x00
    Matrix Orientation;                      // +0x1c  ProjLaunchInfo+0x0c
    Vector Velocity;                         // +0x40  ProjLaunchInfo+0x30
    unsigned int TargetId;                   // +0x4c  guided only: LockTarget's CSimple+0xb0, or 0
    unsigned short TargetSubId;              // +0x50  guided only: ProjLaunchInfo+0x40
    unsigned short Pad52;
};

// SrvGun::launch_bullets' record (.text:06cee23f .. 06cee2b2), for FUN_06d099e0.
struct FServerBulletRecord
{
    unsigned int OwnerId;                    // +0x00
    Archetype::Munition const* Arch;         // +0x04
    unsigned short EquipId;                  // +0x08
    unsigned short Pad0a;
    Vector Position;                         // +0x0c
    Matrix Orientation;                      // +0x18
    Vector Velocity;                         // +0x3c
};

// ---------------------------------------------------------------------------
// Ship -- .text:06d00e80, operator new(0xc8). create() = CObject::Alloc(0x503).
// ---------------------------------------------------------------------------
class Ship : public EqObj
{
public:
    Ship();
    virtual ~Ship();

    bool create(Archetype::Root* Arch) override;
    bool find(unsigned int const& Id) override;

    // Retail LAB_06d01450 (.text:06d01450) — Ship vtable +0x16c.
    bool update(float Dt) override;

    // Retail .text:06d02ae0 -- Ship's override of IObjInspect::get_data (main
    // vtable slot 42, +0xA8): `*OutData = [this+0xb4]; return 0` (RET 0x4).
    // The base IObjInspectImpl::get_data returns null and -3.
    int get_data(void const*& OutData) const override;

    // -- GameObject / EqObj slots Ship overrides (vtable .rdata:06d6711c) ----
    // +0x170 .text:06d01a60 (RET 8): GameObject::enter_system (FUN_06cee6e0,
    // called directly), then DeathSequenceObjId = 0.
    void enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms) override;
    // +0x188 .text:06d01a10 (RET 4): a cause-6 list on a ship nobody owns
    // first drops out of cruise (IObjAffect slot_50(false, true, 0)), then
    // EqObj::apply_damage_list (FUN_06ceb330, direct).
    void apply_damage_list(DamageList& Damage) override;
    // +0x1e4 .text:06d01a90 (RET 0x14): EqObj::light_fuse (FUN_06cec4d0,
    // direct); a lit DEATH_COMM_FUSE is then told to the behaviors of this
    // ship's formation followers (CShip +0x1d4) and follow followers (+0x1f8).
    bool light_fuse(unsigned int SourceId, ID_String const& FuseId,
                    unsigned short SubObjId, float AtT, float Lifetime) override;
    // +0x1f4 .text:06d01c90 (RET 4): EqObj::collect_droppable_equip
    // (FUN_06ce8510, direct), then every EquipDesc still in the jettison queue.
    void collect_droppable_equip(std::vector<EquipDesc>& Out) override;

    // -- IObjAffect overrides (Ship's table .rdata:06d6709c, ctor
    //    .text:06d00f21). Slots 13, 15, 16, 17, 19 and 22 are EqObj's bodies
    //    (.text:06ce68a0 / 06ce6d80 / 06ce6ba0 / 06ce6f60 / 06ce70b0 /
    //    06ce7210) and slot 27 is GameObject's; everything else is here. ----
    int set_throttle(float Throttle) override;                                   //  0 .text:06d01f30
    int set_axis_throttle(Vector const& AxisThrottle) override;                  //  1 .text:06d01f50
    int set_nudge_vec(Vector const& NudgeVec) override;                          //  2 .text:06d01f70
    int set_strafe_dir(StrafeDir Dir) override;                                  //  3 .text:06d01f90
    int set_formation_leader(IObjRW* Leader) override;                           //  4 .text:06d029c0
    int set_formation_offset(Vector const& Offset) override;                     //  5 .text:06d02940
    int add_formation_follower(IObjRW* Follower) override;                       //  6 .text:06d02a50
    int remove_formation_follower(IObjRW* Follower) override;                    //  7 .text:06d02a60
    int get_formation_follower_count() override;                                 //  8 .text:06d02a70
    int set_follow_leader(IObjRW* Leader) override;                              //  9 .text:06d02aa0
    int set_follow_offset(Vector const& Offset) override;                        // 10 .text:06d02a80
    int add_follow_follower(IObjRW* Follower) override;                          // 11 .text:06d02ac0
    int remove_follow_follower(IObjRW* Follower) override;                       // 12 .text:06d02ad0
    int slot_38(unsigned short EquipId, int Count, int SourceClient) override;   // 14 .text:06d020d0
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override; // 18 .text:06d02340
    int slot_50(bool bEngage, bool bArg2, int SourceClient) override;            // 20 .text:06d02410
    int slot_54(bool bActivate, int SourceClient) override;                      // 21 .text:06d02500
    int slot_5c(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                int bFlag, RequestCB* Callback) override;                        // 23 .text:06d02670
    int slot_60(int RequestType, unsigned const* TargetId, int Arg3) override;   // 24 .text:06d027a0
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     bool bFastExit, float Speed) override;                      // 25 .text:06d028c0
    int stop_tradelane() override;                                               // 26 .text:06d02880
    int slot_70(unsigned Id) override;                                          // 28 .text:06d02000
    int slot_74(unsigned Arg) override;                                          // 29 .text:06d02070

    // Retail FUN_06d01e80 (.text:06d01e80, RET 0xc) -- the answer to the
    // request slot_5c raised. Callers: pub::SpaceObj's three response exports
    // (.text:06cfa748 / .text:06cfaa38 / .text:06cfaa98), which pass
    // RequestType 0, 1 and 2 -- the same kinds slot_5c raises controller
    // events 0x12 / 0x19 / 0x14 for. With a PendingRequest callback its
    // OnResult gets the three arguments and a false answer clears the request;
    // without one the owner's client is told through IClient +0x14c (SendServerRequestReturned,
    // REQUEST_RETURNED) and Field_0x98 is re-armed for (0, 3).
    void request_returned(int RequestType, int Response, int Arg3);

    CShip* cship() const;

    // Ship::allocate_equip_id -- retail FUN_06d01c30 (.text:06d01c30), a plain
    // non-virtual thiscall with no stack arguments. The name is retail's own:
    // the failure path FDUMPs "Ship::allocate_equip_id (): Playe..." from
    // E:\FL\Scratch\Source\Server\... line 0x1e5 (.text:06d01c60).
    //   .text:06d01c30  MOV EAX,[ECX + 0x10] / the (ObjectClass & 3) == 3 guard
    //   .text:06d01c44  MOV EDX,[EAX + 0xb4]        ; CSimple::OwnerPlayer
    //   .text:06d01c4c  JNZ -> the FDUMP, then return *SUBOBJ_ID_NONE
    //   .text:06d01c4e  ADD ECX,0xc4
    //   .text:06d01c54  JMP [->COMMON.DLL::SubObjectID::EquipIdMaker::CreateEquipID]
    // i.e. an object that belongs to a PLAYER must take its ids from that
    // player's own PlayerData::equip_id_maker (+0x3C8) instead; only unowned
    // objects mint them here. StarSystem::add_cargo is the single caller.
    unsigned short allocate_equip_id();

    // The 8-byte record FUN_06d010e0 reads: a word (the EquipIdMaker state)
    // at +0 and a dword at +4. Its only builder is StarSystem FUN_06d0a880,
    // which fills a stack copy from its create request just before the call:
    //   .text:06d0ad23  MOV DX,word ptr [EBP + 0xe0]  -> [ESP+0x24]
    //   .text:06d0ad2a  MOV EAX,dword ptr [EBP + 0xe4] -> [ESP+0x28]
    //   .text:06d0ad5e  LEA EAX,[ESP + 0x24] / PUSH EAX / CALL FUN_06d010e0
    struct FEquipState
    {
        SubObjectID::EquipIdMaker EquipIds;   // +0x00
        void const* Data;                     // +0x04  -> Field_0xB4
    };

    // Retail FUN_06d010e0 (.text:06d010e0, RET 4). A DIRECT call of EqObj's
    // +0x1bc body FUN_06ce7d00 (check_fuses; Ship does not override that
    // slot), then the two fields of State. Only caller: FUN_06d0a880
    // (.text:06d0ad65), right after enter_system (+0x170).
    void set_equip_state(FEquipState const& State);

    // Retail FUN_06d01110 (.text:06d01110, RET 0). The one-shot
    // invincibility grant behind the Field_0xB8 latch (see that member).
    // Callers: .text:06cf00d5 (after a player ship is built) and
    // StarSystem FUN_06d0cee0 (.text:06d0cf66, after CShip::launch).
    void apply_launch_invincibility();

    // Retail FUN_06d01150 (.text:06d01150, RET 4) -- the zone-damage tick
    // Ship::update runs last on the live path (.text:06d014bf).
    void tick_zone_damage(float Dt);

    // Retail FUN_06d012e0 (.text:06d012e0, RET 8) -- spreads a damage ratio
    // over hull, equipment and arch groups; true when any of them reported
    // destruction. Only caller: tick_zone_damage (.text:06d01258).
    bool apply_damage_ratio(float Ratio, DamageList& Damage);

    // Retail FUN_06d01550 (.text:06d01550, RET 4) -- the jettison tick
    // Ship::update runs first on the live path (.text:06d014b7). Owns
    // Jettison/JettisonTimer/PendingBayCloses.
    void tick_jettison(float Dt);

    // Retail FUN_06d01860 (.text:06d01860, RET 0xc) -- where a jettisoned item
    // leaves the ship: the bay hardpoints' transform and the direction from
    // bay surface to bay exterior, or the ship's own transform and its -Y
    // axis without them; the direction is scaled to a randomised
    // JETTISONED_CARGO_VELOCITY and the ship's velocity is added. Only caller:
    // tick_jettison (.text:06d016e9).
    void compute_jettison_launch(Vector* OutPos, Matrix* OutOrient, Vector* OutVel);

    // -----------------------------------------------------------------
    // Ship's OWN block, +0x90 .. +0xC7. Decoded 2026-09-17 (wave 9) off the
    // constructor at .text:06d00e80, which writes every one of these in order
    // and nothing else; the EqObj constructor it chains to (.text:06ce6620)
    // stops at `MOV byte ptr [ESI + 0x88],BL` (.text:06ce66b0), so the base
    // really does end at 0x8C/0x90 and this block really is Ship's.
    //
    //   .text:06d00eab  MOV  [ESI + 0x90],EBX(0)
    //   .text:06d00eb1  MOV  [ESI + 0x94],EBX
    //   .text:06d00eb7  MOV  byte [ESI + 0x98],BL
    //   .text:06d00ebd  MOV  [ESI + 0x9c],EBX
    //   .text:06d00ec9  MOV  byte [ESI + 0xa0],AL   <- see the CAUTION below
    //   .text:06d00ec3  PUSH 0x28 / operator new; [EAX]=EAX; [EAX+4]=EAX
    //   .text:06d00edc  MOV  [ESI + 0xa4],EAX       ; _Head of a std::list
    //   .text:06d00ee2  MOV  [ESI + 0xa8],EBX       ; its _Size
    //   .text:06d00ef3  MOV  [ESI + 0xac],EBX
    //   .text:06d00ef9  MOV  [ESI + 0xb0],EBX
    //   (+0xb4 is NEVER written by the constructor)
    //   .text:06d00eff  MOV  byte [ESI + 0xb8],BL
    //   .text:06d00f05  MOV  [ESI + 0xbc],EBX
    //   .text:06d00f0b  MOV  [ESI + 0xc0],EBX
    //   .text:06d00ee8/.text:06d00f11  LEA ECX,[ESI + 0xc4] + a direct call to
    //                   COMMON.DLL::SubObjectID::EquipIdMaker::EquipIdMaker
    // 0xC4 + 4 = 0xC8 = exactly what .text:06d0a8c7's operator new asks for.
    //
    // CAUTION on +0xA0: `MOV AL,[ESP + 0x0b]` at .text:06d00ea5 reads an
    // UNINITIALISED local, not an argument (Ship::Ship takes none). It is
    // MSVC6's 1-byte std::allocator member, copy-initialised from an
    // `allocator()` temporary -- the same artefact as Observer+0x08.
    // Do not read it as data. The triple {+0xA0, +0xA4, +0xA8} is one
    // std::list whose nodes are 0x28 bytes (0x08 header + a 0x20 value).
    //
    // DECODED 2026-09-17 (wave 13). The 2026-09-17 (wave 9) note said "what
    // each field MEANS is still open"; it is not any more, and FOUR of them
    // were typed wrong -- +0x94, +0xAC, +0xBC and +0xC0 are FLOATS, read with
    // FLD/FST/FSUB, not `unsigned int`. +0x9C is a pointer, not an int.
    //
    // The readers, in order of how much they settle:
    //
    //  * Ship::update, retail's vtable slot at .rdata:06d67288 ->
    //    .text:06d01450. `if (Field_0x90 == 0)` is the live path -- it runs
    //    CObject::update (.text:06d014b1, with CObject::NO_SCANNER for an
    //    unowned ship and CObject::UPDATE_DUMB for a player's outside single
    //    player), then the jettison tick and the damage tick. When +0x90 is
    //    non-zero the ship is running its DEATH SEQUENCE instead:
    //      .text:06d014cb  single player: wait for CShip+0x214, then
    //                      .text:06d014e3 FUN_06d00130 and +0x90 = 0
    //      .text:06d014fc  multiplayer: bail while CShip+0x2bd is set,
    //      .text:06d01506  FLD [ESI+0x94] / FSUB dt / FST [ESI+0x94], and when
    //                      the result is <= 0.0 re-enable collisions
    //                      (.text:06d01525), FUN_06d00130, +0x94 = 0, +0x90 = 0.
    //  * the system-level destroy handler at .text:06d0cad1 is what ARMS it:
    //    `MOV [EBP+0x90],EAX` right after `CALL [EDX+0x20]`, i.e. the id of the
    //    object whose destruction started the sequence, and then
    //    .text:06d0cb81 / .text:06d0cb95 store the delay --
    //    .rdata:06d67cc4 == 0.0f normally, .rdata:06d67cc0 == 0x41400000 ==
    //    12.0f when get_type_bits() has bit 0x40 or bit 0x800 set.
    //  * FUN_06d01e80 (.text:06d01e80), reached from .text:06cfa748 /
    //    .text:06cfaa38 / .text:06cfaa98 with a leading 0, 1 and 2, owns +0x98
    //    and +0x9C: when +0x9C is null it answers the client directly through
    //    IClient +0x14c at .text:06d01f04 (SendServerRequestReturned, ch2 op 0x44
    //    FLPACKET_SERVER_REQUEST_RETURNED)
    //    and sets +0x98 to (kind == 0 && status == 3); when +0x9C is set it
    //    calls THAT object's vtable slot 0 with the same three arguments and
    //    clears both on a false answer (.text:06d01ea9 / .text:06d01eaf).
    //    .text:06cfa731 reads +0x98 back.
    //  * FUN_06d01550 (.text:06d01550), the jettison tick, owns +0xA0..+0xB0:
    //    +0xAC counts down by dt and is re-armed from
    //    COMMON.DLL::JETTISON_CLEARANCE_TIME after every ejection, +0xB0 is how
    //    many CShip::close_bay calls are still owed and is drained one per tick
    //    once the queue empties (.text:06d015bb).
    //  * FUN_06d01150 (.text:06d01150), the damage tick, owns +0xBC/+0xC0:
    //    +0xBC accumulates `fuse rate * dt` (.text:06d011a8 FADD /
    //    .text:06d011ae FSTP) and +0xC0 accumulates dt (.text:06d011bc FADD /
    //    .text:06d011c2 FST). Once +0xBC is non-zero and +0xC0 > 1.0 the
    //    accumulated damage is divided by the hull's own vtable +0xd4 value and,
    //    above 1%, applied through FUN_06d012e0; +0xBC is then zeroed
    //    (.text:06d01270). Nothing in the module ever resets +0xC0.
    //
    // These offsets are RETAIL's. This port's GameObject base is not
    // byte-compatible with retail's, so the members below reproduce the ORDER
    // and the types, not the absolute addresses. Names for what retail leaves
    // anonymous are the port's own, as bShipGone is in server_sysdb.h.
    unsigned int DeathSequenceObjId;      // +0x90  an object id; 0 == the live path
    float DeathSequenceTimer;             // +0x94  seconds; armed to 12.0f or 0.0f
    // +0x98 -- deliberately left anonymous. FUN_06d01e80 sets it to
    // `(param_1 == 0 && param_2 == 3)`:
    //   .text:06d01f0a  85ff          TEST EDI,EDI          ; EDI == param_1
    //   .text:06d01f0c  c68698...00   MOV  byte [ESI+0x98],0
    //   .text:06d01f14  750c          JNZ  LAB_06d01f22
    //   .text:06d01f16  83fd03        CMP  EBP,0x3          ; EBP == param_2
    //   .text:06d01f19  7507          JNZ  LAB_06d01f22
    //   .text:06d01f1b  c68698...01   MOV  byte [ESI+0x98],1
    // and .text:06cfa731 reads it back. So the CONDITION is known but not what
    // the two literals mean. A Field_0xNN with a confirmed primitive type is
    // what this port allows in that situation; do not name it on a guess.
    unsigned char Field_0x98;
    IObjAffect::RequestCB* PendingRequest; // +0x9C  slot_5c stores it; request_returned calls its OnResult (vtable slot 0)
    // +0xA0..+0xA8 -- the jettison queue, a COMMON.DLL EquipDescList (a VC6
    // std::list<EquipDesc>: allocator +0xA0, head +0xA4, size +0xA8, 0x28-byte nodes
    // with the EquipDesc at +0x08). The type is proved by the unwind funclets:
    // Unwind@06d60ecb (Ship::Ship, ECX = this + 0xa0) and Unwind@06d60ef8
    // (~Ship, ECX = CBase + 0x94 = this + 0xa0) both JMP to the import
    // COMMON.DLL::EquipDescList::~EquipDescList (.rdata:06d643f0). Ship::Ship builds it
    // inline (.text:06d00ec3..06d00ee2: operator new(0x28) head, size 0) before the
    // EquipIdMaker at +0xC4; ~Ship destroys it after its body (.text:06d00fc0..06d01016)
    // and before ~EqObj -- a member destroyed by the compiler, as here.
    EquipDescList Jettison;
    float JettisonTimer;                  // +0xAC  JETTISON_CLEARANCE_TIME countdown
    int PendingBayCloses;                 // +0xB0
    // +0xB4 -- the constructor leaves it alone; FUN_06d010e0 (.text:06d010f9)
    // is the only writer, copying dword 1 of the same two-field record whose
    // word 0 seeds EquipIds (.text:06d010ef). Its only reader is get_data
    // (.text:06d02ae0, main vtable slot 42 +0xA8), which hands it out as the
    // IObjInspect `void const*` data pointer -- hence the type.
    void const* Field_0xB4;
    // +0xB8 -- a ONE-SHOT latch, not a toggle. FUN_06d01110 (.text:06d01110,
    // called from .text:06cf00d5 and .text:06d0cf66) is its whole story:
    //   .text:06d01110  8a81b8000000  MOV  AL,byte ptr [ECX + 0xb8]
    //   .text:06d0111b  752c          JNZ  LAB_06d01149      ; already done
    //   .text:06d0111d  b801000000    MOV  EAX,0x1
    //   .text:06d01126  8881b8000000  MOV  byte ptr [ECX + 0xb8],AL
    //   .text:06d01143  ff9094010000  CALL dword ptr [EAX + 0x194]
    // i.e. "fire the +0x194 slot exactly once per ship", with a 0x0C-byte
    // FInvincibility built on the stack: dword 1 (.text:06d0112c), byte 1
    // (.text:06d01130), byte 1 (.text:06d01134), float 0.0f (.text:06d0113b)
    // -- not {1, 0, 0} as this note used to say. ECX is the Ship BASE: the
    // vtable it calls through is `[ECX]`, at +0x00.
    unsigned char Field_0xB8;
    float AccumulatedDamage;              // +0xBC
    float AccumulatedDamageTime;          // +0xC0

    // Retail Ship+0xC4, the last dword of the 0xC8 bytes Ship::Ship allocates
    // (.text:06d00e80, `operator new(200)`).
    SubObjectID::EquipIdMaker EquipIds;   // +0xC4
};

// ---------------------------------------------------------------------------
// Solar -- create() = CObject::Alloc(0x303), .text:06d02c50.
// ---------------------------------------------------------------------------
class Solar : public EqObj
{
public:
    Solar();
    // .text:06d02bc0 (the body; .text:06d02b90 is the deleting destructor):
    // re-stamps Solar's three vtables, calls Solar's own +0x15c slot
    // (`CALL [.rdata:06d675c8]` = EqObj::destroy_notify, .text:06ce7c80) on
    // this, then ~EqObj (FUN_06ce6710).
    virtual ~Solar();

    bool create(Archetype::Root* Arch) override;                                  // +0x164 .text:06d02c50
    bool find(unsigned int const& Id) override;                                   // +0x168 .text:06d02cb0 `XOR AL,AL / RET 4`

    // FUN_06d02c20 (.text:06d02c20, RET 4; the name is the port's). Wraps a
    // CObject that already exists: CBase::Object = Obj, CObject::AddRef(Obj),
    // CBase::advise(true), return Object != 0. Only caller: StarSystem::load's
    // single-player walk FUN_06d07170 (.text:06d07206).
    bool attach_existing(CObject* Obj);

    // -- Solar's overrides in its main vtable .rdata:06d6746c ---------------
    bool update(float Dt) override;                                              // +0x16c .text:06d02cc0
    bool destroy(int Kind, unsigned int ObjId) override;                         // +0x158 .text:06d02d10
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;  // +0x17c .text:06d02d40
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;  // +0x180 .text:06d02d90
    void explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out) override; // +0x1fc .text:06d02f20

    // +0x250 .text:06d03070 (RET 0xc) -- Solar's own slot, the only one past
    // EqObj's +0x24c. The name is retail's: its error path FDUMPs "Invalid type
    // in ChooseSystemArrivalLocation()" from E:\FL\Scratch\Source\Server\Space\
    // Solar.cpp line 0xeb (.text:06d03111). Callers: SystemSwitchOutComplete
    // (.text:06cef876) and pub::SpaceObj::JumpIn (.text:06cfa30f), both on the
    // arrival solar, both pushing the arriving object first -- which this body
    // never reads.
    virtual void ChooseSystemArrivalLocation(GameObject* Arriving, Vector& OutPos, Matrix& OutOrient);

    CSolar* csolar() const;
};

// ---------------------------------------------------------------------------
// Loot -- create() = CObject::Alloc(0x803), .text:06cf2440: narrows the fresh
// CObject with (ObjectClass & 0x803) == 0x803 only for the open() call, then
// CBase::advise(true), Archetype::Root::create_instance, DALib::Engine +0x6c,
// and returns get_index() != -1 (see Loot::create for the two port gaps).
// ---------------------------------------------------------------------------
class Loot : public GameObject
{
public:
    Loot();
    virtual ~Loot();

    bool create(Archetype::Root* Arch) override;
    bool find(unsigned int const& Id) override;

    // +0x154 override, .text:06d08eb0 (Loot vtable .rdata:06d67df4 slot 85 ->
    // .rdata:06d67f48): `MOV EAX,0x10000000 / RET` -- a constant, NOT the base
    // body's cobj()->TypeBits.
    unsigned int get_type_bits() const override;

    // Loot's own overrides (vtable .rdata:06d67df4): +0x158 FUN_06cf2660, +0x16c
    // LAB_06cf24c0, +0x188 FUN_06cf2750. Bodies in src/Server/server_loot.cpp.
    bool destroy(int Kind, unsigned int ObjId) override;
    bool update(float Dt) override;
    void apply_damage_list(DamageList& Damage) override;

    // FUN_06cf2910 (.text:06cf2910, RET 4) -- commodity decay, called only by
    // update (.text:06cf2514). False when the last unit decayed and the loot was
    // destroyed, true otherwise; the caller ignores it.
    bool update_decay(float Dt);
    // FUN_06cf2b10 (.text:06cf2b10, RET 0) -- the archetype's explosion_arch as a
    // cause-4 blast through StarSystem::explode; destroy(1, ...) only.
    void explode_on_destroy();
    // FUN_06cf2c10 (.text:06cf2c10, RET 0) -- a CargoPod whose contents exceed
    // UnitsPerContainer splits into at most five new loot records, spawned
    // through StarSystem::create_loot (mode 2); destroy(1, ...) only.
    void split_stack();

    CLoot* cloot() const;

    // The three dwords the retail spawner's inlined constructor zeroes after
    // GameObject's (.text:06d08bb3-06d08bb9); operator new(0x70) at .text:06d08b90.
    float CheckTimer;   // +0x60  seconds since the last unseen-radius scan (every 3 s)
    float UnseenLife;   // +0x64  counts down; reset to LOOT_UNSEEN_LIFE_TIME near a player
    float DecayAccum;   // +0x68  decayed hit points not yet taken off a unit
};

// ---------------------------------------------------------------------------
// Projectile -- RTTI ".?AUProjectile@@" (.data:06d88554). The abstract base of
// Mine, Munition/Guided, CounterMeasure and Bullet (their base arrays at
// .rdata:06d6c6b8 / 06d6c73c list it right after the leaf). It adds nothing:
// its own vtables (.rdata:06d662d4 / 06d66254 / 06d6624c, stored by the inlined
// constructor in every leaf's, e.g. .text:06cf3354-06cf3361) are GameObject's
// slot for slot (create/find still _purecall .text:06d5fff0), and every leaf
// destructor goes straight into GameObject's (FUN_06cee4a0, .text:06cf34bb).
// ---------------------------------------------------------------------------
class Projectile : public GameObject
{
};

// ---------------------------------------------------------------------------
// Mine -- RTTI ".?AUMine@@" (.data:06d88814), operator new(0xa0) in the system's
// mine spawner (.text:06d08f30). Base array .rdata:06d6c6b8: Projectile (and its
// GameObject chain), PhySys::Controller at +0x60 (COL offset 0x60, vtable
// .rdata:06d6622c) and PhySys::CObjectCB at +0x70 (COL .rdata:06d6c700, offset
// 0x70, vtable .rdata:06d65fd8 = {FUN_06cf3990, FUN_06cf3a50}).
// Bodies in src/Server/server_mine.cpp. Primary vtable .rdata:06d6606c; against
// GameObject's it overrides +0x154 (06cf3400), +0x158 (06cf3870), +0x164
// (06cf34e0), +0x168 (06d02cb0), +0x16c (06cf36a0), +0x17c (06cf4230) and +0x180
// (06cf4240) -- nothing else.
// ---------------------------------------------------------------------------
class Mine : public Projectile, public PhySys::Controller, public PhySys::CObjectCB
{
public:
    // FUN_06cf3330 (.text:06cf3330).
    Mine();
    // FUN_06cf3440 (.text:06cf3440), entered on the CBase sub-object; FUN_06cf3410 /
    // LAB_06cf3c60 are its deleting forms.
    virtual ~Mine();

    unsigned int get_type_bits() const override;              // .text:06cf3400
    bool destroy(int Kind, unsigned int ObjId) override;      // .text:06cf3870
    bool create(Archetype::Root* Arch) override;              // .text:06cf34e0
    bool find(unsigned int const& Id) override;               // .text:06d02cb0
    bool update(float Dt) override;                           // .text:06cf36a0
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;   // .text:06cf4230
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;   // .text:06cf4240

    // PhySys::CObjectCB (+0x70): FUN_06cf3990 / FUN_06cf3a50.
    void added(CObject* Obj) override;
    void deleted(CObject* Obj) override;

    // FUN_06cf3560 (.text:06cf3560, RET 0). The proximity phantom and, for a seeking
    // mine, the controller registration. Only caller: the spawner, right after
    // enter_system (.text:06d090df). The name is the port's.
    void arm();

    // CMine narrowing: (ObjectClass & 0xf) == 0xf, as .text:06cf36c8-06cf36d2.
    CMine* cmine() const;

protected:
    // PhySys::Controller slot 4, LAB_06cf3aa0 (RET 8): accelerate toward the target.
    void do_simulation_controller(IVP_Event_Sim* EventSim,
                                  IVP_U_Vector<IVP_Core>* CoreList) override;

public:
    void* Phantom;                  // +0x74  PhySys phantom handle (CreatePhantom)
    unsigned int IntruderCount;     // +0x78  objects inside the phantom (Slot00 ++ / Slot04 --)
    unsigned int Field_0x7c;        // +0x7c  never written or read by Mine's bodies
    bool bSeeking;                  // +0x80  update sets it while a target is in range
    Vector Target;                  // +0x84  closest intruder position (GetClosestPhantomIntruderPos)
    float SeekTimer;                // +0x90  constructor FLT_MAX; update adds Dt, zeroed past 1.0
    bool bTriggered;                // +0x94  Slot00 latch: a hostile ship touched the phantom
    unsigned int TriggerObjId;      // +0x98  that ship's id (CSimple+0xb0)
    unsigned int Field_0x9c;        // +0x9c  never written or read by Mine's bodies
};

// ---------------------------------------------------------------------------
// Munition -- RTTI ".?AUMunition@@" (.data:06d88828). The shared base of Guided and
// Bullet; its base array (.rdata:06d6c734) is Munition, Projectile and GameObject's
// chain. Like Projectile it adds nothing: its own vtables (.rdata:06d667ac /
// 06d6672c / 06d66720, stored by Guided's constructor at .text:06cf3d04-06cf3d11)
// end at +0x1b8 (.rdata:06d66964) with GameObject's bodies and _purecall for
// create/find. Guided and Bullet each add their OWN slot +0x1bc, with different
// arities (Guided RET 0, Bullet RET 8), so it is not a Munition virtual.
// ---------------------------------------------------------------------------
class Munition : public Projectile
{
};

// ---------------------------------------------------------------------------
// Guided -- RTTI ".?AUGuided@@" (.data:06d88840), a guided missile; operator new(0x80)
// in the system's missile spawner (.text:06d096f6) and constructed by FUN_06cf3ce0.
// Base array .rdata:06d6c808: Guided, Munition, Projectile, GameObject's chain, then
// PhySys::Controller at +0x60 (vtable .rdata:06d66700) and PhySys::CObjectCB at +0x70
// (vtable .rdata:06d664a4 = {LAB_06cf4ae0, .text:06ce1050 `RET 4`}).
// Primary vtable .rdata:06d6653c; against GameObject's it overrides +0x154
// (06cf3d70), +0x158 (06cf4450), +0x164 (06cf3e50), +0x168 (06cf3ed0), +0x16c
// (06cf3f10), +0x17c (06cf4230), +0x180 (06cf4240), +0x1a4 (06cf4270) and adds
// +0x1bc (06cf4310); IObjAffect table .rdata:06d664bc overrides +0x48 set_target
// (06cf4c10). Bodies in src/Server/server_guided.cpp.
// ---------------------------------------------------------------------------
class Guided : public Munition, public PhySys::Controller, public PhySys::CObjectCB
{
public:
    // FUN_06cf3ce0 (.text:06cf3ce0).
    Guided();
    // FUN_06cf3db0 (.text:06cf3db0), entered on the CBase sub-object; FUN_06cf3d80 /
    // LAB_06cf4ec0 are its deleting forms.
    virtual ~Guided();

    unsigned int get_type_bits() const override;              // .text:06cf3d70
    bool destroy(int Kind, unsigned int ObjId) override;      // .text:06cf4450
    bool create(Archetype::Root* Arch) override;              // .text:06cf3e50
    bool find(unsigned int const& Id) override;               // .text:06cf3ed0
    bool update(float Dt) override;                           // .text:06cf3f10
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;   // .text:06cf4230
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;   // .text:06cf4240
    void on_collision(FCollisionEvent const& Hit) override;   // .text:06cf4270

    // +0x1bc, LAB_06cf4310 (RET 0). Registers the controller and builds the proximity
    // phantom. Only caller: the missile spawner, right after enter_system
    // (.text:06d09848 `CALL [EAX+0x1bc]`). The name is the port's.
    virtual void arm();

    // IObjAffect +0x48, LAB_06cf4c10 (RET 0xc).
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override;

    // PhySys::CObjectCB (+0x70): LAB_06cf4ae0 / .text:06ce1050 (`RET 4`).
    void added(CObject* Obj) override;
    void deleted(CObject* Obj) override;

    // CGuided narrowing: (ObjectClass & 0xa7) == 0xa7, as .text:06cf3f35-06cf3f41.
    CGuided* cguided() const;

protected:
    // PhySys::Controller slot 4, LAB_06cf4b50 (RET 8): the motor's thrust.
    void do_simulation_controller(IVP_Event_Sim* EventSim,
                                  IVP_U_Vector<IVP_Core>* CoreList) override;

    // FUN_06cf45a0 (.text:06cf45a0, RET 8): turn toward the target (proportional
    // navigation on the line of sight) and keep the velocity on the nose. Only caller:
    // update (.text:06cf41df). The name is the port's.
    void seek(Vector const& TargetPos, Vector const& TargetVel);

public:
    void* Phantom;                  // +0x74  PhySys phantom handle (CreatePhantom)
    bool bTriggered;                // +0x78  Slot00 latch: something touched the phantom
    unsigned int TriggerObjId;      // +0x7c  its root's id (0 for a non-CSimple root)
};

// ---------------------------------------------------------------------------
// Bullet -- RTTI ".?AUBullet@@" (.data:06d88b80), a gun round (CBeam, class 0x67).
// operator new(0x60) in the system's bullet spawner FUN_06d099e0 (.text:06d099ff),
// which inlines the constructor: GameObject's, then the three vtables
// (.text:06d09a1e-06d09a2b); no field of its own. Base array .rdata:06d6d130:
// Bullet, Munition, Projectile, GameObject's chain. Primary vtable .rdata:06d680d4;
// against GameObject's it overrides +0x154 (.text:06d09b90), +0x164 (06cf4d40), +0x168
// (06d02cb0), +0x16c (06cf4dc0), +0x17c/+0x180/+0x184 (.text:06d28880 `RET 8`), +0x1a4
// (06cf4e40) and adds +0x1bc (.text:06d28880 `RET 8`). Its CBase slot 0 is the
// shared FUN_06d08ec0. Bodies in src/Server/server_guided.cpp.
// ---------------------------------------------------------------------------
class Bullet : public Munition
{
public:
    unsigned int get_type_bits() const override;              // .text:06d09b90
    bool create(Archetype::Root* Arch) override;              // .text:06cf4d40
    bool find(unsigned int const& Id) override;               // .text:06d02cb0
    bool update(float Dt) override;                           // .text:06cf4dc0
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;          // .text:06d28880
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;          // .text:06d28880
    void explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage) override;  // .text:06d28880
    void on_collision(FCollisionEvent const& Hit) override;   // .text:06cf4e40

    // +0x1bc, .text:06d28880 `RET 8`: Bullet's own two-argument slot, an empty body. No
    // call through a Bullet's +0x1bc exists in Server.dll; name and argument types are
    // not recoverable from the empty body.
    virtual void Slot1bc(unsigned int Arg1, unsigned int Arg2);

    // CBeam narrowing: (ObjectClass & 0x67) == 0x67, as .text:06cf4d7e-06cf4d87.
    CBeam* cbeam() const;
};

// ---------------------------------------------------------------------------
// CounterMeasure -- RTTI ".?AUCounterMeasure@@" (.data:06d88578), a dropped decoy
// (CCounterMeasure, class 0x17). operator new(0x60) in the system's spawner
// FUN_06d092c0 (.text:06d092f0 .. 06d09310), constructed by FUN_06ce6170: GameObject's
// constructor, then the three vtables (.text:06ce6178 .. 06ce6185); no field of its
// own. Base array (COL at .rdata:06d64fc8): CounterMeasure, Projectile, GameObject's
// chain. Primary vtable .rdata:06d64fcc (ends at +0x1b8); against GameObject's it
// overrides +0x154 (06ce61f0), +0x158 (06ce63f0), +0x164 (06ce6200), +0x168
// (06d02cb0, Mine's), +0x16c (06ce6280) and puts .text:06cf4230 in BOTH +0x17c and
// +0x180. Its IObjAffect table (.rdata:06d64f4c) is Bullet's entry for entry
// (GameObject's bodies) and its CBase slot 0 is the shared FUN_06d08ec0.
// Bodies in src/Server/server_countermeasure.cpp.
// ---------------------------------------------------------------------------
class CounterMeasure : public Projectile
{
public:
    // FUN_06ce6170 (.text:06ce6170, RET 0).
    CounterMeasure();

    unsigned int get_type_bits() const override;              // .text:06ce61f0
    bool destroy(int Kind, unsigned int ObjId) override;      // .text:06ce63f0
    bool create(Archetype::Root* Arch) override;              // .text:06ce6200
    bool find(unsigned int const& Id) override;               // .text:06d02cb0
    bool update(float Dt) override;                           // .text:06ce6280
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;   // .text:06cf4230
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;   // .text:06cf4230

    // FUN_06ce6300 (.text:06ce6300, RET 0). The decoy pass: every guided missile
    // of another owner inside the archetype's Range may be retargeted onto this
    // countermeasure (DiversionPctg). Only caller: the spawner, right after
    // enter_system (.text:06d094ac). The name is the port's.
    void divert_missiles();

    // CCounterMeasure narrowing: (ObjectClass & 0x17) == 0x17, as .text:06ce629f-06ce62a7.
    CCounterMeasure* ccountermeasure() const;
};

// ---------------------------------------------------------------------------
// SrvAsteroid::MedAsteroid / SrvAsteroid::MedMine -- the server objects that
// stand in for a live asteroid / mine of an active asteroid-field cube. RTTI
// ".?AVMedAsteroid@SrvAsteroid@@" (.data:06d88aa0) and
// ".?AVMedMine@SrvAsteroid@@" (.data:06d88ac8). Created only by the StarSystem
// factories FUN_06d09bb0 / FUN_06d09c80 on behalf of
// SrvAsteroid::FSrvAsteroidCube::apply_marker (.text:06d04ab0) and kept in the
// system's +0xc0 block (StarSystem::ListC0). Bodies in server_classes.cpp.
//
// Their vtables were dumped slot by slot against GameObject's (.rdata:06d657ec
// main, .rdata:06d6576c IObjAffect, .rdata:06d65764 CBase). Everything not
// overridden below is GameObject's own entry:
//   main +0x164 create   .text:06d02cb0  `XOR AL,AL / RET 4` (both)
//   main +0x168 find     .text:06d04c30  (both)
//   main +0x16c update   MedMine only, .text:06d04e50 (MedAsteroid keeps
//                        GameObject's FUN_06cee9e0)
//   main +0x17c/+0x180/+0x184  .text:06d28880 `RET 8` (both) -- the ICF body
//                        the listing labels IServerImpl::Connect
//   main +0x188          MedAsteroid only, .text:06ce1050 `RET 4` (labelled
//                        CAccount::AppendCharacterNames); MedMine keeps
//                        GameObject's FUN_06ceee70
//   main +0x1bc          MedMine only: a NEW slot (112 entries against
//                        GameObject's 111), .text:06d28880 `RET 8`
//   CBase slot 0         the scalar deleting destructors .text:06d04bd0 /
//                        .text:06d04cb0
// The IObjInspect part (+0x000..+0x160) and the whole IObjAffect table are
// GameObject's in both, entry for entry.
// ---------------------------------------------------------------------------
namespace SrvAsteroid {

// operator new(0x68) at .text:06d09bc8.
class MedAsteroid : public GameObject
{
public:
    // FUN_06d04ba0 (.text:06d04ba0): GameObject's constructor, then
    // +0x64 = 0 and byte +0x60 = 0.
    MedAsteroid();
    // FUN_06d04c00 (.text:06d04c00), entered on the CBase sub-object: frees
    // +0x64 when it is set, then GameObject's destructor (.text:06d04c2a JMP).
    virtual ~MedAsteroid();

    bool create(Archetype::Root* Arch) override;       // .text:06d02cb0
    bool find(unsigned int const& Id) override;        // .text:06d04c30
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;          // .text:06d28880
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;          // .text:06d28880
    void explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage) override;  // .text:06d28880
    void apply_damage_list(DamageList& Damage) override;                                  // .text:06ce1050

    // +0x60 / +0x64. The constructor clears both, the destructor frees +0x64
    // (FUN_06d5ffb0, operator delete) when it is non-null, and nothing else in
    // Server.dll writes either: no MedAsteroid slot differs from GameObject's
    // except the ones listed above, none of which touches them. Their meaning
    // is therefore not recoverable from this binary; the types are the ones the
    // constructor and destructor use (a byte, and a heap pointer).
    unsigned char Field_0x60;
    void* Field_0x64;
};

// operator new(0x88) at .text:06d09c98. A second base, PhySys::CObjectCB, sits
// at +0x60 (COL .rdata:06d6cf3c, offset 0x60); its vtable .rdata:06d67a54 holds
// the two phantom callbacks.
class MedMine : public GameObject, public PhySys::CObjectCB
{
public:
    // FUN_06d04c70 (.text:06d04c70): GameObject's constructor, +0x80 = +0x7c =
    // 1.0f, the CObjectCB vtable at +0x60, +0x78 = 0. The phantom (+0x64), the
    // intruder count (+0x68), the direction (+0x6c) and +0x84 are left alone
    // until arm() fills them.
    MedMine();
    // FUN_06d04ce0 (.text:06d04ce0): PhySys::DestroyPhantom(+0x64) -- no null
    // test -- then +0x64 = 0, then GameObject's destructor.
    virtual ~MedMine();

    bool create(Archetype::Root* Arch) override;       // .text:06d02cb0
    bool find(unsigned int const& Id) override;        // .text:06d04c30
    bool update(float Dt) override;                    // .text:06d04e50
    void damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage) override;          // .text:06d28880
    void munition_hit(FMunitionImpact const& Hit, DamageList& Damage) override;          // .text:06d28880
    void explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage) override;  // .text:06d28880

    // +0x1bc, .text:06d28880 `RET 8`: MedMine's own two-argument slot, the 112th entry of
    // its main vtable (.rdata:06d67ca8, right before the float data at .rdata:06d67cac);
    // MedAsteroid's table ends at +0x1b8. An empty body, like Bullet's own +0x1bc. No call
    // through a MedMine's +0x1bc exists in Server.dll; name and argument types are not
    // recoverable from the empty body.
    virtual void Slot1bc(unsigned int Arg1, unsigned int Arg2);

    // PhySys::CObjectCB. added = LAB_06d04fb0, deleted = LAB_06d05070.
    void added(CObject* Obj) override;
    void deleted(CObject* Obj) override;

    // FUN_06d04d60 (.text:06d04d60, thiscall, RET 8): (radius, system id).
    // Only caller apply_marker (.text:06d04b30), right after the factory.
    void arm(float DetectRadius, unsigned int SystemId);

    void* Phantom;                                // +0x64  PhySys phantom handle
    unsigned int IntruderCount;                   // +0x68  ships inside (unsigned: JBE at .text:06d04e78)
    Vector Direction;                             // +0x6c  unit vector mine -> last ship that entered
    Archetype::Explosion const* Explosion;        // +0x78  GetExplosion(arch ExplosionArch)
    float RechargeTimer;                          // +0x7c
    float RechargeTime;                           // +0x80  Archetype::Asteroid::RechargeTime
    float ExplosionOffset;                        // +0x84  Archetype::Asteroid::ExplosionOffset
};

} // namespace SrvAsteroid

// ---------------------------------------------------------------------------
// The dying-ship list -- retail's file-static std::list<Ship*> in ship.cpp:
// allocator byte .data:DAT_06d8d8e0, _Head .data:DAT_06d8d8e4, _Size
// .data:DAT_06d8d8e8, 0x0C-byte nodes {Next, Prev, Ship*}.
//   FUN_06d00090  CRT static initialiser: sentinel = operator new(0xc),
//                 self-linked, size 0, atexit(FUN_06d000d0)
//   FUN_06d000d0  atexit teardown: free every node, then the sentinel; zero
//                 head and size
//   FUN_06d00130  push_back(Ship*) -- Ship::update's death-sequence arm
//                 (.text:06d014e3 / .text:06d0152c)
//   FUN_06d00180  remove(Ship*) -- every matching node; Ship::~Ship
//                 (.text:06d00fae)
// Drained by Server_PhysicsUpdate (FUN_06d001d0, .text:06d001e7 .. 06d0026b):
// controller event 0x25 {ship id, DeathSequenceObjId, system id} per ship,
// then the list is emptied.
// A global std::list IS that initialiser/teardown pair: the compiler emits the
// same construct-then-atexit(destroy) sequence for it.
// ---------------------------------------------------------------------------
extern std::list<Ship*> GDyingShips;
void Ship_QueueDying(Ship* DyingShip);      // FUN_06d00130
void Ship_ForgetDying(Ship* DyingShip);     // FUN_06d00180

// ---------------------------------------------------------------------------
// Objects between star systems. Retail keeps them in a global std::list at
// DAT_06d8d8d4 (allocator byte) / DAT_06d8d8d8 (head node) / DAT_06d8d8dc
// (size); a node is 0x40 bytes: next, prev, then this 0x38-byte element.
// CRT init LAB_06d002e0 builds it and registers FUN_06d00320 (its destructor)
// with atexit. The system-transfer path (.text:06cfa4fa) parks an object here
// when the target system is null; the lookup below searches it between the
// cached system and SysDB::SysMap. Defined in src/Server/server_classes.cpp.
// ---------------------------------------------------------------------------
struct FInTransitObject
{
    PhySys::CreateParms Parms;   // +0x00  the physics state leave_system handed back
    GameObject* Obj;             // +0x34
};
static_assert(offsetof(FInTransitObject, Obj) == 0x34, "FInTransitObject: Obj at +0x34");
extern std::list<FInTransitObject> GInTransitObjects;

// FUN_06d00380 (cdecl): the first entry holding Obj is taken out -- its Parms
// copied to OutParms, the node freed -- and the lookup cache's watcher is
// cleared when it watches Obj. False when Obj is not in the list. Callers:
// .text:06cfa2f6, .text:06cfa44d (system transfer).
bool Server_TakeInTransit(GameObject* Obj, PhySys::CreateParms& OutParms);

// FUN_06d00410 (cdecl): as above without the copy, then controller message 0x39
// (FObjectDestroyedEvent, only ObjId set) and the wrapper is deleted. False when
// Obj is not in the list. Only caller: pub::SpaceObj::Destroy (.text:06cf864d).
bool Server_DestroyInTransit(GameObject* Obj);

// FUN_06d00560 (cdecl): push_back {Parms, Obj}. Only caller: .text:06cfa4fa.
void Server_PushInTransit(GameObject* Obj, PhySys::CreateParms const& Parms);

// FUN_06cffdc0 (cdecl, no arguments; only caller FUN_06d001d0 at .text:06d001e2): the
// pub::Phantom event pump -- controller events 0x2d / 0x2e for every id a phantom saw
// enter / leave since the last tick. Defined in server_pub.cpp next to the phantom record.
void Server_PumpPhantomEvents();

// ---------------------------------------------------------------------------
// The object lookup by id. Server-internal: retail Server.dll exports neither.
// ---------------------------------------------------------------------------
// FUN_06d006c0 (cdecl, 21 call sites): the one-entry cache (GLookupCacheObject /
// GLookupCacheSystem, server_sysdb.h), then the cached system's find_object,
// then GInTransitObjects, then every other system in SysDB::SysMap. OutSys is
// the system the object lives in, null for an in-transit object. On success
// the cache is re-pointed at the result.
bool Server_FindObject(unsigned int const& Id, GameObject*& OutObj, StarSystem*& OutSys);

// FUN_06d00630 (cdecl, 13 call sites): Server_FindObject, then false -- with
// OutObj reset to null, OutSys left as the lookup wrote it -- when the object's
// own +0x160 (is_destroyed) answers true.
bool Server_FindLiveObject(unsigned int Id, GameObject*& OutObj, StarSystem*& OutSys);

// Server_FindObject for the callers that only keep the object: retail passes a
// dummy for OutSys at those sites. A false return always leaves OutObj null,
// so the object pointer carries the whole answer. Server_LookupObject
// (server_cobject.h) is the same with the result flattened to void*. Defined in
// src/Server/server_pub.cpp.
GameObject* Server_LookupGameObject(unsigned int ObjId);

// Retail FUN_06d00670 (.text:06d00670), __cdecl(unsigned int Flid): the lookup
// above, then null when the object's own +0x160 (is_destroyed) answers true.
// Defined in src/Server/server_pub.cpp.
GameObject* Server_LookupLiveObject(unsigned int Flid);

// ---------------------------------------------------------------------------
// The launch placement for a player leaving a base (ServerSpace.cpp). Retail
// FUN_06d00830 (.text:06d00830, cdecl, 5 args, 380 insns); only caller
// IServerImpl::PlayerLaunch (.text:06cefcbd). Retail name not recovered.
// OutObjId gets the object the ship launches from (0 when none), OutKind the
// launch_pos index (-1 when the ship is not launched from an object), OutPos /
// OutOrient the launch transform. Clears Players[PlayerId].exited_base_id when
// the player was in a base. Defined in src/Server/server_classes.cpp.
// ---------------------------------------------------------------------------
class Quaternion;
void Server_GetLaunchPosition(unsigned int PlayerId, unsigned int* OutObjId, int* OutKind,
                              Vector* OutPos, Quaternion* OutOrient);

// DAT_06d8d900 -- the free-terminal mailbox pub::ReportFreeTerminal writes and
// Server_GetLaunchPosition reads back after controller event 0x32. Defined in
// src/Server/server_pub.cpp.
extern int GReportedFreeTerminal;
