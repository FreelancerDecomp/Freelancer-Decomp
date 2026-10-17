// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Server.dll fuse database and fuse-action classes -- retail .text:06cfc650-06cffc97
// (RTTI: ServerFuse, ServerFuseAction, IgniteFuseAction, DestroyRootAction,
// DamageRootAction, DestroyGroupAction, DamageGroupAction,
// DestroyHardpointAttachmentAction, DamageHardpointAttachmentAction, ImpulseAction,
// TumbleAction, MakeInvincibleAction, DumpCargoAction -- .data:06d886c8 .. 06d88a68).
//
// Common.dll owns only the Fuse / FuseAction base classes and the list of fuse INI paths
// (FuseDB::m_FuseINIFiles). Server.dll reads those files itself (FUN_06cfd4b0) into its own
// map of ServerFuse prototypes (DAT_06d8d870), creating one Server FuseAction subclass per
// action section. An EqObj ignites a fuse by copy-constructing the prototype into its own burn
// list (EqObj light_fuse, .text:06cec4d0, vtable +0x1e4) and binding itself as the owner
// (FUN_06cfd1e0). The freelancer.exe counterpart of this file is
// src/freelancer/Client/client_fuse.h.
//
// Port status: the database (load / teardown / accessor), the INI reader, the owner binding,
// every constructor / clone / Read / slot-12 copy and every Trigger / UnTrigger override follow
// the listing.
//
// Every damage Trigger builds its DamageList the same way (retail inlines the DamageList
// constructor): cause 0x15, inflictor = the source id, inflictor owner = the source object's
// CSimple+0xb4 when the owner sits in a StarSystem that knows the source (FUN_06d0c8d0, mask
// 0x7f0000). The EqObj / GameObject slots they call (+0x188, +0x194, +0x1c0 .. +0x1e8) are
// declared in server_gameobject.h.

#include "common_archetype.h" // CacheString
#include "common_fuses.h"
#include "common_ini.h"       // INI_Reader, Vector

#include <map>

class EqObj;

// ---------------------------------------------------------------------------
// ServerFuse -- vftable .rdata:06d65724: [.text:06cec7c0 scalar deleting dtor (Fuse::~Fuse +
// delete), Fuse::FreeActions]. sizeof 0x3c = Fuse (0x34) + Owner +0x34 + bHasDestroyRoot +0x38.
// ---------------------------------------------------------------------------
class ServerFuse : public Fuse
{
public:
    // FUN_06cfd4e0 .text:06cfd583-06cfd5aa: Fuse(ID_String{CreateID("")}, -1.0f), +0x34 = 0,
    // +0x38 = 0.
    ServerFuse(ID_String const& Nickname = ID_String{CreateID("")}, float Lifetime = -1.0f);
    // EqObj light_fuse .text:06cec59c-06cec5b9 (inlined): the Common copy constructor (which
    // clones every action through FuseAction slot 1), then +0x34 and +0x38 copied.
    ServerFuse(ServerFuse const& Other);
    // .text:06cec7c0: Fuse::~Fuse only.
    ~ServerFuse() override;

    // FUN_06cfc650 -- reads one [fuse] section and the action sections that follow it.
    bool Read(INI_Reader& Reader);
    // FUN_06cfd1e0 -- stores the owning object in the fuse and in every action.
    void SetOwner(EqObj* NewOwner);

    EqObj* Owner;                   // +0x34
    // +0x38. Set by Read when a [destroy_root] section is met (.text:06cfc711); read by EqObj
    // light_fuse (.text:06cec61c) to decide whether igniting this fuse kills the object.
    bool bHasDestroyRoot;
};

// ---------------------------------------------------------------------------
// ServerFuseAction -- vftable .rdata:06d66ea8, 12 slots: FUN_06cfcf40 scalar deleting dtor,
// _purecall x2, every other slot the Common.dll FuseAction body. Only adds the owner pointer at
// +0x24. Abstract, like FuseAction.
// ---------------------------------------------------------------------------
class ServerFuseAction : public FuseAction
{
public:
    // Inlined at every construction site (e.g. .text:06cfc6cd-06cfc6d8): FuseAction(),
    // +0x24 = 0.
    ServerFuseAction();
    // FUN_06cfd1b0: the inlined ~FuseAction (restore the Common vftable, free the sub-object
    // vector, zero its three pointers). FUN_06cfcf40 is its scalar deleting wrapper, shared by
    // every subclass vftable.
    ~ServerFuseAction() override;

    EqObj* Owner; // +0x24
};

// Slot 12 (+0x30) of every action subclass below except DestroyRootAction is a new virtual
// that copies the archetype part of another instance of the same class
// (FuseAction::CopyArchProperties + the subclass fields); the slot-1 clones call it on the fresh
// object. Its retail name is unknown, hence Slot30 (the same convention as client_fuse.h).

// ---------------------------------------------------------------------------
// IgniteFuseAction -- vftable .rdata:06d66e70, sizeof 0x30 ([ignite_fuse]).
// ---------------------------------------------------------------------------
class IgniteFuseAction : public ServerFuseAction
{
public:
    IgniteFuseAction();

    FuseAction* Slot04() const override;       // FUN_06cfcea0
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfefd0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // FUN_06cff050
    // LAB_06cff0e0 -- the only UnTrigger override in the family (slot 8).
    void UnTrigger(unsigned short SubObjId) override;

protected:
    virtual void Slot30(IgniteFuseAction const& Other); // FUN_06cff150

public:
    ID_String FuseId; // +0x28 "fuse"
    float FuseT;      // +0x2c "fuse_t"
};

// ---------------------------------------------------------------------------
// DestroyRootAction -- vftable .rdata:06d66e2c, sizeof 0x28 ([destroy_root]). No slot 12: the
// clone copies through CopyArchProperties (slot 11) directly.
// ---------------------------------------------------------------------------
class DestroyRootAction : public ServerFuseAction
{
public:
    DestroyRootAction();

    FuseAction* Slot04() const override;                          // LAB_06cfcb30
    bool Slot08(INI_Reader& Reader) override;                     // FUN_06cfe040
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // FUN_06cfe070
};

// ---------------------------------------------------------------------------
// DamageRootAction -- vftable .rdata:06d66de8, sizeof 0x30 ([damage_root]).
// ---------------------------------------------------------------------------
class DamageRootAction : public ServerFuseAction
{
public:
    DamageRootAction();

    FuseAction* Slot04() const override;       // LAB_06cfcb90
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfe0f0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cfe180

protected:
    virtual void Slot30(DamageRootAction const& Other); // FUN_06cfe330

public:
    int Hitpoints;    // +0x28 "hitpoints"
    bool bCanKill;   // +0x2c "damage_type": stricmp "absolute" -> 0, anything else -> 1; the bCanKill of EqObj +0x1c0/+0x1c4/+0x1cc
};

// ---------------------------------------------------------------------------
// DestroyGroupAction -- vftable .rdata:06d66da0, sizeof 0x30 ([destroy_group]).
// ---------------------------------------------------------------------------
class DestroyGroupAction : public ServerFuseAction
{
public:
    DestroyGroupAction();

    FuseAction* Slot04() const override;       // FUN_06cfcc00
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfe370
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cfe490

protected:
    virtual void Slot30(DestroyGroupAction const& Other); // FUN_06cff150 (ICF)

public:
    CacheString GroupName; // +0x28 "group_name"
    int Fate;              // +0x2c "fate": "debris" -> 2, "disappear" -> 3
};

// ---------------------------------------------------------------------------
// DamageGroupAction -- vftable .rdata:06d66d58, sizeof 0x34 ([damage_group]).
// ---------------------------------------------------------------------------
class DamageGroupAction : public ServerFuseAction
{
public:
    DamageGroupAction();

    FuseAction* Slot04() const override;       // FUN_06cfcca0
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfe6c0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cfe7d0

protected:
    virtual void Slot30(DamageGroupAction const& Other); // FUN_06cfe950

public:
    CacheString GroupName; // +0x28 "group_name"
    int Hitpoints;         // +0x2c "hitpoints"
    bool bCanKill;        // +0x30 "damage_type": exact "absolute" -> 0, anything else -> 1; the bCanKill of EqObj +0x1c0/+0x1c4/+0x1cc
};

// ---------------------------------------------------------------------------
// DestroyHardpointAttachmentAction -- vftable .rdata:06d66d08, sizeof 0x30
// ([destroy_hp_attachment]).
// ---------------------------------------------------------------------------
class DestroyHardpointAttachmentAction : public ServerFuseAction
{
public:
    DestroyHardpointAttachmentAction();

    FuseAction* Slot04() const override;       // FUN_06cfcd50
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfe9a0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cfeae0

protected:
    virtual void Slot30(DestroyHardpointAttachmentAction const& Other); // FUN_06cff150 (ICF)

public:
    CacheString Hardpoint; // +0x28 "hardpoint"
    int Fate;              // +0x2c "fate": "debris" -> 2, "disappear" -> 3, "loot" -> 5
};

// ---------------------------------------------------------------------------
// DamageHardpointAttachmentAction -- vftable .rdata:06d66cb8, sizeof 0x34
// ([damage_hp_attachment]).
// ---------------------------------------------------------------------------
class DamageHardpointAttachmentAction : public ServerFuseAction
{
public:
    DamageHardpointAttachmentAction();

    FuseAction* Slot04() const override;       // FUN_06cfcdf0
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cfed40
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cfee50

protected:
    virtual void Slot30(DamageHardpointAttachmentAction const& Other); // FUN_06cfe950 (ICF)

public:
    CacheString Hardpoint; // +0x28 "hardpoint"
    int Hitpoints;         // +0x2c "hitpoints"
    bool bCanKill;        // +0x30 "damage_type": exact "absolute" -> 0, anything else -> 1; the bCanKill of EqObj +0x1c0/+0x1c4/+0x1cc
};

// ---------------------------------------------------------------------------
// ImpulseAction -- vftable .rdata:06d66c78, sizeof 0x44 ([impulse]).
// ---------------------------------------------------------------------------
class ImpulseAction : public ServerFuseAction
{
public:
    ImpulseAction();

    FuseAction* Slot04() const override;       // FUN_06cfcf60
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cff170
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cff290

protected:
    virtual void Slot30(ImpulseAction const& Other); // FUN_06cff550

public:
    CacheString Hardpoint; // +0x28 "hardpoint"
    float Force;           // +0x2c "force"
    float Radius;          // +0x30 "radius"
    float Damage;          // +0x34 "damage"
    Vector PosOffset;      // +0x38 "pos_offset"
};

// ---------------------------------------------------------------------------
// TumbleAction -- vftable .rdata:06d66c38, sizeof 0x50 ([tumble]).
// ---------------------------------------------------------------------------
class TumbleAction : public ServerFuseAction
{
public:
    TumbleAction();

    FuseAction* Slot04() const override;       // LAB_06cfd010
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cff5a0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cff6c0

protected:
    virtual void Slot30(TumbleAction const& Other); // FUN_06cff940

public:
    float AngDragScale;      // +0x28 "ang_drag_scale" (1.0f by default)
    // "turn_throttle_x/y/z": value 0 goes to TurnThrottleLow, value 1 to TurnThrottleHigh.
    Vector TurnThrottleLow;  // +0x2c (0, 0, 0 by default)
    Vector TurnThrottleHigh; // +0x38 (1, 1, 1 by default)
    float ThrottleLow;       // +0x44 "throttle" value 0
    float ThrottleHigh;      // +0x48 "throttle" value 1
    bool bEngineKill;        // +0x4c "engine_kill"
};

// ---------------------------------------------------------------------------
// MakeInvincibleAction -- vftable .rdata:06d66bf0, sizeof 0x2c ([make_invincible]).
// ---------------------------------------------------------------------------
class MakeInvincibleAction : public ServerFuseAction
{
public:
    MakeInvincibleAction();

    FuseAction* Slot04() const override;       // LAB_06cfd0a0
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cff970
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cff9d0

protected:
    virtual void Slot30(MakeInvincibleAction const& Other); // FUN_06cffa80

public:
    bool bTurnOn; // +0x28 "turn_on" (1 by default)
};

// ---------------------------------------------------------------------------
// DumpCargoAction -- vftable .rdata:06d66bac, sizeof 0x2c ([dump_cargo]).
// ---------------------------------------------------------------------------
class DumpCargoAction : public ServerFuseAction
{
public:
    DumpCargoAction();

    FuseAction* Slot04() const override;       // FUN_06cfd110
    bool Slot08(INI_Reader& Reader) override;  // FUN_06cffaa0
    void Trigger(unsigned SourceId, unsigned short SubObjId) override; // LAB_06cffb10

protected:
    virtual void Slot30(DumpCargoAction const& Other); // FUN_06cffc80

public:
    CacheString OriginHardpoint; // +0x28 "origin_hardpoint"
};

// ---------------------------------------------------------------------------
// The Server fuse prototype database: std::map<unsigned, ServerFuse*> at .data:DAT_06d8d870,
// keyed by the fuse nickname id. Derives from FuseDB to read the protected path list.
// ---------------------------------------------------------------------------
class ServerFuseDB : public FuseDB
{
public:
    static std::map<unsigned, ServerFuse*>& Get(); // FUN_06cfd390
    static void Clear();                           // FUN_06cfd3a0
    static void LoadAll();                         // FUN_06cfd4b0

private:
    static void LoadFile(char const* Path);        // FUN_06cfd4e0 (thiscall on the map)

    static std::map<unsigned, ServerFuse*> Fuses;  // DAT_06d8d870
};

// "random" (.rdata:06d66f18) interned once at load time: the group / hardpoint name the
// DestroyGroupAction (LAB_06cfe350 -> DAT_06d8d8ac) and DestroyHardpointAttachmentAction
// (LAB_06cfe980 -> DAT_06d8d8a8) Trigger bodies compare against with stricmp.
extern char const* GRandomGroupName;     // DAT_06d8d8ac
extern char const* GRandomHardpointName; // DAT_06d8d8a8
