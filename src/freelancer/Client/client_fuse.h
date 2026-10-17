// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// freelancer.exe fuse database and fuse-action classes -- retail .text:00529e10-0052c7f1
// (source path Client\Space\...; RTTI names ClientFuse, ClientFuseAction, StartExplosionAction,
// StartCameraParticlesAction, StartEffectAction, ClientImpulseAction, ClientIgniteFuseAction).
//
// Common.dll only owns the Fuse / FuseAction base classes and the list of fuse INI paths
// (FuseDB::m_FuseINIFiles, filled by FuseDB::AddFuseINIFile). The exe reads those files itself
// (FUN_0052a7a0) into its own map of ClientFuse prototypes (DAT_00675400), creating one exe
// FuseAction subclass per action section. A ship ignites a fuse by copy-constructing the
// prototype into its own burn list (FUN_00534240, client_equip_fx.cpp).
//
// Port status: the database, the INI reader, the host binding, every constructor / clone / Read /
// slot-12 copy, the UnTriggers, every action's Trigger and every Load / UnLoad follow the listing.
// Slots a retail class does not override fall through to the Common.dll FuseAction bodies.

#include "common_archetype.h" // CacheString
#include "common_csys.h"      // Matrix
#include "common_fuses.h"

#include <map>
#include <vector>

class FClientGameobj;
class INI_Reader;
struct FEffectArch; // vis_effect_arch.h

// ---------------------------------------------------------------------------
// ClientFuse -- ??_7ClientFuse@@6B@ (.rdata:005dd328): [FUN_0052aab0 scalar deleting dtor
// (Fuse::~Fuse + delete), Fuse::FreeActions]. sizeof 0x38 = Fuse (0x34) + Host at +0x34.
// ---------------------------------------------------------------------------
class ClientFuse : public Fuse
{
public:
    // FUN_0052a7d0 .text:0052a873-0052a89a: Fuse(ID_String{CreateID("")}, -1.0f), Host = 0.
    ClientFuse(ID_String const& Nickname = ID_String{CreateID("")}, float Lifetime = -1.0f);
    // FUN_00534240 .text:005342d0-005342e4 (and the node copy in FUN_00535a20): the Common copy
    // constructor (which clones every action through FuseAction slot 1), then +0x34 copied.
    ClientFuse(ClientFuse const& Other);
    // FUN_0052aab0: Fuse::~Fuse only.
    ~ClientFuse() override;

    // FUN_00529e10 -- reads one [fuse] section and the action sections that follow it.
    bool Read(INI_Reader& Reader);
    // FUN_0052a4d0 -- stores the host game object in the fuse and in every action.
    void BindHost(FClientGameobj* NewHost);

    FClientGameobj* Host; // +0x34
};

// ---------------------------------------------------------------------------
// ClientFuseAction -- ??_7ClientFuseAction@@6B@ (.rdata:005dd2e4), 12 slots: FUN_0052a410
// scalar deleting dtor, _purecall x2, every other slot the Common.dll FuseAction body. Only adds
// the host pointer at +0x24. Abstract, like FuseAction.
// ---------------------------------------------------------------------------
class ClientFuseAction : public FuseAction
{
public:
    // Inlined at every construction site (e.g. .text:00529e92-00529e9b): FuseAction(), +0x24 = 0.
    ClientFuseAction();
    // FUN_0052a250: the inlined ~FuseAction (restore the Common vftable, free the sub-object
    // vector). FUN_0052a410 is its scalar deleting wrapper.
    ~ClientFuseAction() override;

    FClientGameobj* Host; // +0x24

protected:
    // LAB_0052b430 -- the UnTrigger body shared (one address) by StartExplosionAction and
    // StartCameraParticlesAction's vftable slot 8. Unlike Common's FuseAction::UnTrigger it also
    // clears bTriggered once the sub-object list is empty.
    void UnTriggerAndSettle(unsigned short SubObjId);
};

// Slot 12 (+0x30) of every action subclass below is a new virtual that copies the archetype part
// of another instance of the same class (FuseAction::CopyArchProperties + the subclass fields);
// the slot-1 clones call it on the fresh object. Its retail name is unknown, hence Slot30.

// ---------------------------------------------------------------------------
// StartExplosionAction -- vftable .rdata:005dd2ac, sizeof 0x40 ([start_explosion]).
// ---------------------------------------------------------------------------
class StartExplosionAction : public ClientFuseAction
{
public:
    StartExplosionAction();

    FuseAction* Slot04() const override;       // FUN_0052a0f0
    bool Slot08(INI_Reader& Reader) override;  // FUN_0052b020
    // FUN_0052b290: the explosion archetype resolved into +0x3c and AddRef'd (FUN_00536100),
    // then FuseAction::Load (tail JMP).
    int Load() override;
    // FUN_0052b2c0: the archetype released (FUN_00536170) when set, then FuseAction::UnLoad.
    int UnLoad() override;
    // LAB_0052b150 (RET 8): unless already triggered, a process-2 explosion starts at the
    // host's hardpoint plus pos_offset as an Action_Explode (FUN_005361e0) on the host's action
    // list; then the FuseAction::Trigger tail (inlined in retail).
    void Trigger(unsigned SourceId, unsigned short SubObjId) override;
    void UnTrigger(unsigned short SubObjId) override; // LAB_0052b430

protected:
    virtual void Slot30(StartExplosionAction const& Other); // FUN_0052b110

public:
    ID_String Explosion;                      // +0x28 "explosion"
    CacheString Hardpoint;                    // +0x2c "hardpoint"
    Vector PosOffset;                         // +0x30 "pos_offset"
    Archetype::Explosion* ExplosionArch;      // +0x3c set by Load (FUN_0052b290)
};

// ---------------------------------------------------------------------------
// StartCameraParticlesAction -- vftable .rdata:005dd260, sizeof 0x30 ([start_cam_particles]).
// ---------------------------------------------------------------------------
class StartCameraParticlesAction : public ClientFuseAction
{
public:
    StartCameraParticlesAction();

    FuseAction* Slot04() const override;       // FUN_0052a1b0
    bool Slot08(INI_Reader& Reader) override;  // FUN_0052b2e0
    // FUN_0052b4a0: +0x2c = FUN_0040f080(+0x28), a found arch referenced (FUN_0040e960(0)); then
    // FuseAction::Load (tail JMP).
    int Load() override;
    // FUN_0052b4d0: +0x2c released (FUN_0040e990) when set, the pointer left as it is; then
    // FuseAction::UnLoad (tail JMP).
    int UnLoad() override;
    // LAB_0052b370 (RET 8): unless already triggered, a named effect is started as a world
    // one-shot (FUN_00503f40 on the CameraParticles factory DAT_00674c5c) at the host's
    // transform; then the FuseAction::Trigger tail (inlined in retail).
    void Trigger(unsigned SourceId, unsigned short SubObjId) override;
    void UnTrigger(unsigned short SubObjId) override; // LAB_0052b430

protected:
    virtual void Slot30(StartCameraParticlesAction const& Other); // FUN_0052b350 (ICF)

public:
    ID_String Effect;         // +0x28 "effect"
    FEffectArch* EffectArch;  // +0x2c set by Load (FUN_0040f080); Trigger looks the id up again
};

// One [start_effect] "hardpoint" entry: the 0x34-byte element of StartEffectAction+0x30.
struct FStartEffectHardpoint
{
    CacheString Hardpoint; // +0x00
    Matrix Orientation;    // +0x04 "ori_offset" (identity by default, FUN_004215b0)
    Vector PosOffset;      // +0x28 "pos_offset"
};

// ---------------------------------------------------------------------------
// StartEffectAction -- vftable .rdata:005dd218, sizeof 0x54 ([start_effect]).
// ---------------------------------------------------------------------------
class StartEffectAction : public ClientFuseAction
{
public:
    StartEffectAction();
    // FUN_0052a340 -> FUN_0052b510: the effect companion at +0x44 released (twice: the inlined
    // release, then the companion member's own destructor), the hardpoint vector freed, then
    // ~FuseAction.
    ~StartEffectAction() override;

    FuseAction* Slot04() const override;       // FUN_0052a280
    bool Slot08(INI_Reader& Reader) override;  // FUN_0052b5f0
    // FUN_0052bf40: +0x40 = FUN_0040f080(+0x28), a found arch referenced (FUN_0040e960(0)); then
    // FuseAction::Load (tail JMP).
    int Load() override;
    // FUN_0052bf70: +0x40 released (FUN_0040e990) when set, the pointer left as it is; then
    // FuseAction::UnLoad (tail JMP).
    int UnLoad() override;
    // LAB_0052bb00 (RET 8): unless already triggered, the effect starts at the host's root or
    // at one of the listed hardpoints (random start, first that resolves) -- attached to the
    // host's effect list (FUN_00503070) or as a world one-shot (FUN_00503f40); then the
    // FuseAction::Trigger tail (inlined in retail).
    void Trigger(unsigned SourceId, unsigned short SubObjId) override;
    void UnTrigger(unsigned short SubObjId) override; // FUN_0052be90

protected:
    virtual void Slot30(StartEffectAction const& Other); // FUN_0052b8c0

public:
    ID_String Effect;                               // +0x28 "effect"
    bool bAttached;                                 // +0x2c "attached"
    std::vector<FStartEffectHardpoint> Hardpoints;  // +0x30
    FEffectArch* EffectArch;                        // +0x40 set by Load (FUN_0040f080)
    // +0x44: the effect companion {slot0, sound, particle, light} (the layout FUN_00503070 fills
    // and FUN_004fcef0 / FUN_00502d90 release). Constructed zero (FUN_005154a0(0,0,0,0)).
    void* Companion[4]; // +0x44
};

// ---------------------------------------------------------------------------
// ClientImpulseAction -- vftable .rdata:005dd1d8, sizeof 0x40 ([impulse]).
// ---------------------------------------------------------------------------
class ClientImpulseAction : public ClientFuseAction
{
public:
    ClientImpulseAction();

    FuseAction* Slot04() const override;       // FUN_0052a360
    bool Slot08(INI_Reader& Reader) override;  // FUN_0052bf90
    // LAB_0052c0c0 (RET 8): unless already triggered, every ObjDB kind-10 object within radius
    // of the host's hardpoint (+ pos_offset) gets a linear impulse (PhySys::LinearImpulse);
    // then the FuseAction::Trigger tail (inlined in retail).
    void Trigger(unsigned SourceId, unsigned short SubObjId) override;

protected:
    virtual void Slot30(ClientImpulseAction const& Other); // FUN_0052c080

public:
    CacheString Hardpoint; // +0x28 "hardpoint" (NOT copied by Slot30 -- retail)
    float Force;           // +0x2c "force"
    float Radius;          // +0x30 "radius"
    Vector PosOffset;      // +0x34 "pos_offset"
};

// ---------------------------------------------------------------------------
// ClientIgniteFuseAction -- vftable .rdata:005dd194, sizeof 0x30 ([ignite_fuse]).
// ---------------------------------------------------------------------------
class ClientIgniteFuseAction : public ClientFuseAction
{
public:
    ClientIgniteFuseAction();

    FuseAction* Slot04() const override;       // FUN_0052a430
    bool Slot08(INI_Reader& Reader) override;  // FUN_0052c340
    int Load() override;                       // LAB_0052c3b0
    int UnLoad() override;                     // LAB_0052c420

protected:
    virtual void Slot30(ClientIgniteFuseAction const& Other); // FUN_0052b350 (ICF)

public:
    ID_String FuseId;  // +0x28 "fuse"
    bool bFuseLoaded;  // +0x2c
};

// ---------------------------------------------------------------------------
// The exe's fuse prototype database: std::map<unsigned, ClientFuse*> at .data:DAT_00675400,
// keyed by the fuse nickname id. Derives from FuseDB to read the protected path list.
// ---------------------------------------------------------------------------
class ClientFuseDB : public FuseDB
{
public:
    static std::map<unsigned, ClientFuse*>& Get(); // FUN_0052a680
    static void Clear();                           // FUN_0052a690
    static void LoadAll();                         // FUN_0052a7a0

    // The `find` + `->second` every retail caller inlines (FUN_00432240 / FUN_0052c7a0 then
    // [node+0x10]). On a miss retail reads the value slot of the VC6 tree head node, which
    // _Buynode never initialises; the port returns nullptr there, and every retail caller
    // tests the loaded pointer for null before use.
    static ClientFuse* Find(unsigned FuseId);

private:
    static void LoadFile(char const* Path);        // FUN_0052a7d0 (thiscall on the map)

    static std::map<unsigned, ClientFuse*> Fuses;  // DAT_00675400
};

// One node of the host game object's burning-fuse list (FClientGameobj+0xc4, a VC6 std::list
// of ClientFuse; retail node 0x40 = next, prev, ClientFuse 0x38).
struct FFuseBurnNode
{
    FFuseBurnNode* Next;
    FFuseBurnNode* Prev;
    ClientFuse Live;
};
