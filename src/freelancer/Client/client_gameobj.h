// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_mesh_load.h" // FMeshHolder (Ship +0x108, FUN_00420d40)
#include "common_archetype.h"
#include "common_csys.h" // Transform (Ship +0x118)
#include "common_cequip.h" // ActionDB, INotify
#include "common_cobject.h"
#include "common_iface.h"
#include "common_misc.h" // DamageList
#include "common_tstring.h" // TString (Solar +0xe8)
#include "font_string.h" // FFontString (GameObject +0x48)

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>

struct FCliEquipFirePacket; // cli_projectile_fx.h
struct FEffectArch;         // vis_effect_arch.h
struct FParticleEffect;
struct FStarSphere;         // star_sphere.h (Solar +0xe4)
struct ISoundSource;
namespace Archetype { struct ISound; }

// ===========================================================================
// The client's GameObject family -- retail E:\FL\Scratch\Source\Client\space\Gameobj.cpp.
//
// RTTI and constructors (sizes from the operator_new calls, vtables from the ctor stores):
//   GameObject  : IObjInspectImpl  ctor FUN_005392d0(kind)  0xac  IObjRW 005de7ec
//                                  IObjAffect 005de76c, CBase 005de764 -> FUN_00539480
//   Simple      : GameObject       ctor FUN_00547440(kind)  0xac  IObjRW 005e00a4
//                                  IObjAffect 005e0024, CBase 005e0018 -> FUN_00547470
//   EquippedObj : Simple, INotify  ctor FUN_00532bc0(kind)  0xe4  IObjRW 005de23c
//                                  IObjAffect 005de1bc, CBase 005de1b0 -> FUN_00532d00,
//                                  INotify at +0xac 005de1a8 -> FUN_00534730
//   Ship        : EquippedObj      ctor FUN_00543380        0x190 IObjRW 005dfd6c
//                                  IObjAffect 005dfcec, CBase 005dfce0 -> FUN_00543500,
//                                  INotify 005dfcd8 -> FUN_00545670
//   Solar       : EquippedObj      ctor FUN_005475b0        0x208 IObjRW 005e0304
//                                  IObjAffect 005e0284, CBase 005e0278 -> FUN_00547650,
//                                  INotify 005e0270 -> FUN_005478f0
// Projectile / Munition / Beam / Mine / CounterMeasure / Guided and Loot are declared after
// Ship (below); Debris lives in client_debris.h. The asteroid branches are not modelled yet.
//
// THE EXTENSION VTABLE IS DECLARED IN RETAIL SLOT ORDER. IObjInspect's 85 slots come first
// (common_iface.h), so the first virtual below is slot 85 (+0x154) and every following one is
// the next retail slot; the byte offset and the base body sit next to each. Neither
// IObjInspectImpl's nor these classes' destructors add a primary slot -- they override CBase's
// (the +0x0c sub-object), where retail keeps them. Do not insert a virtual between two of these
// without a retail slot for it. Slot names are not recovered (the exe exports none of them):
// `create`, `find` and `update` borrow Server.dll GameObject's names for the slots of the same
// shape and meaning (server_gameobject.h); every other slot is `SlotNNN` after its byte offset.
// ===========================================================================

enum class EClientGameobjKind : int
{
    None = 0,
    Ship = 1,
    Solar = 2,
    Beam = 3, // FUN_0052dde0 pushes 3 into FUN_0052e100 (0x0052de9f)
    Guided = 4, // FUN_0053d080 pushes 4 into FUN_00542550 (0x0053d0cd)
    Mine = 5, // FUN_0053ca40 pushes 5 into FUN_00542550 (0x0053ca8d)
    CounterMeasure = 6, // FUN_0053cd70 pushes 6 into FUN_00542550 (0x0053cdbd)
    Loot = 7, // FUN_0053e470 pushes 7 into FUN_00547440 (0x0053e471)
    MedAsteroid = 8, // FUN_00524cb0 pushes 8 into FUN_005392d0 (cli_asteroid_med.h)
    DynamicAsteroid = 9, // FUN_00526650 pushes 9 into FUN_005392d0 (cli_asteroid_dynamic.h)
    Debris = 10, // FUN_00530620 / FUN_005306e0 push 0xa into FUN_005392d0 (0x00530656)
};

// GameObject+0x28 -- the gameobj's effect list: a VC6 std::list whose node is 0x18 bytes
// (next, prev, then the 0x10-byte effect companion {slot0, ISoundSource*, particle, light}).
// FUN_004fcea0 inserts, FUN_004fcef0 removes one, FUN_004fd0d0 clears, FUN_004fd040 sweeps
// (cli_projectile_fx.h / space_scene_obj_draw.cpp).
//
// Two layers, as retail's EH tables show: FGameobjEffectListStorage is the bare list (its
// constructor is inlined at 0x00539335..0x00539353, its destructor is FUN_0053aa40 and the
// unwind action of FUN_005393e0 while FUN_004fd0d0 runs); FGameobjEffectList is the member
// whose destructor is FUN_005393e0 -- FUN_004fd0d0(0) and then the storage's destructor.
// FUN_005392d0 unwinds a half-built GameObject through FUN_005393e0 (Unwind@005c0aa8), so the
// list is a real sub-object with its own destructor, not a teardown written into the owner.
struct FGameobjEffectListStorage
{
    explicit FGameobjEffectListStorage(unsigned char InAllocatorByte);
    ~FGameobjEffectListStorage();
    FGameobjEffectListStorage(FGameobjEffectListStorage const&) = delete;
    FGameobjEffectListStorage& operator=(FGameobjEffectListStorage const&) = delete;

    // FUN_0053aab0 (thiscall, RET 8: out iterator, Where): unlink Where, release its
    // companion (FUN_00502d90's sequence), free the node, --Count; returns the next node.
    void* Erase(void* Where);

    unsigned char AllocatorByte; // +0x00  the ctor copies the kind's low byte here
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel node, operator_new(0x18), next/prev = self
    int Count;                   // +0x08
};

struct FGameobjEffectList : public FGameobjEffectListStorage
{
    explicit FGameobjEffectList(unsigned char InAllocatorByte)
        : FGameobjEffectListStorage(InAllocatorByte)
    {
    }
    // FUN_005393e0.
    ~FGameobjEffectList();
};
static_assert(sizeof(FGameobjEffectList) == 0x0c, "GameObject effect list 0xc");

// EquippedObj+0xb4 -- the list of equipment-effect wrappers (VC6 std::list<ClientEquip*>, node
// 0xc {next, prev, wrapper}). Its constructor is inlined in FUN_00532bc0 at 0x00532bf0..
// 0x00532c12 (EH state 0 -> 1), its destructor is the shared list destructor FUN_004a2d30 --
// the unwind action of the EquippedObj constructor (Unwind@005c07cb) and destructor
// (Unwind@005c0808) -- and FUN_00532d40 inlines it (FUN_004a2f80 over [begin, end), head
// freed) at 0x00532f5a..0x00532f8e. The nodes' wrappers are not deleted by it (the owner's
// destructor deletes them first, FUN_00533080).
struct FEquipFxList
{
    explicit FEquipFxList(unsigned char InAllocatorByte);
    ~FEquipFxList();
    FEquipFxList(FEquipFxList const&) = delete;
    FEquipFxList& operator=(FEquipFxList const&) = delete;

    unsigned char AllocatorByte; // +0x00  the kind's low byte
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel, operator_new(0xc), next = prev = itself
    int Count;                   // +0x08
};
static_assert(sizeof(FEquipFxList) == 0x0c, "EquippedObj equip-FX list 0xc");

// EquippedObj+0xc0 -- the list of burning fuses (VC6 std::list<ClientFuse>, node 0x40 =
// FFuseBurnNode {next, prev, ClientFuse}). Its constructor is inlined in FUN_00532bc0 at
// 0x00532c18..0x00532c39 (EH state 1 -> 2), its destructor is FUN_005359c0 -- the unwind action
// of both the EquippedObj constructor (Unwind@005c07d9) and destructor (Unwind@005c0816) --
// and FUN_00532d40 inlines it (FUN_00535a80 over [begin, end), head freed) at 0x00532f27..57.
struct FEquippedFuseList
{
    explicit FEquippedFuseList(unsigned char InAllocatorByte);
    ~FEquippedFuseList();
    FEquippedFuseList(FEquippedFuseList const&) = delete;
    FEquippedFuseList& operator=(FEquippedFuseList const&) = delete;

    unsigned char AllocatorByte; // +0x00  the kind's low byte
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel, operator_new(0x40), only the links written
    int Count;                   // +0x08
};
static_assert(sizeof(FEquippedFuseList) == 0x0c, "EquippedObj fuse list 0xc");

// GameObject+0x84 element: the destructor stops (vt+0x60, ISoundSource::Stop) and releases
// (vt+0x8) the interface at +4 of every 8-byte element.
struct FGameobjSound
{
    unsigned Field_0x00;  // +0x00  not read by any function ported so far
    ISoundSource* Source; // +0x04
};
static_assert(sizeof(FGameobjSound) == 8, "GameObject sound element 8 bytes");

// EquippedObj+0xcc map value (retail node +0x10 / +0x14): the target object id and its
// sub-object. A new entry is {0, SUBOBJ_ID_NONE} (0x005356d5..0x005356f3,
// 0x00535806..0x0053582d).
struct FEquipSubTarget
{
    FEquipSubTarget() : TargetId(0), TargetSubObj(SUBOBJ_ID_NONE) {}

    unsigned TargetId;            // +0x00 (node +0x10)
    unsigned short TargetSubObj;  // +0x04 (node +0x14)
};

class FClientGameobj : public IObjInspectImpl
{
public:
    // FUN_005392d0 (RET 4). Kind lands in ManagerKind and its low byte in the two allocator
    // bytes (+0x28, +0x80).
    explicit FClientGameobj(EClientGameobjKind Kind);
    // FUN_005394b0 (this = the CBase sub-object); deleting wrapper FUN_00539480.
    ~FClientGameobj() override;

    // IObjInspect slot 8 (+0x20) LAB_00524e70 -- every client gameobj vtable holds it (15
    // XREFs: 005de80c GameObject, 005dca34 MedAsteroid, 005dccfc DynamicAsteroid, ...): the
    // object narrowed to a CSimple ((class & 3) == 3) gives its Id (+0xb0); no object or another
    // class gives 0. Common's IObjInspectImpl::get_id has no such test.
    unsigned int const get_id() const override;

    // ---- the extension vtable, retail slot order --------------------------------------
    // 85 +0x154 LAB_00524e20: bit 2 of Flags (set by destroy(1)).
    virtual bool Slot154() const;
    // 86 +0x158 LAB_00524e30: bit 0 of Flags. FUN_005424e0 (the ObjDB reap) removes and deletes
    // every gameobj that answers true.
    virtual bool Slot158() const;
    // 87 +0x15c FUN_00539c10 (RET 4): bit 0 of Flags = the argument; then, when there is an
    // object, CObject::enable_collisions_r(false) (a tail JMP, whatever the argument was).
    virtual void Slot15c(bool bValue);
    // 88 +0x160 LAB_00524e40: bit 1 of Flags.
    virtual bool Slot160() const;
    // 89 +0x164 LAB_00539c40 (RET 4): bit 1 of Flags = the argument.
    virtual void Slot164(bool bValue);
    // 90 +0x168 LAB_005396f0 (RET 4): CObject::Alloc(1) into Object, Arch->create_instance,
    // DALib::Engine vt+0x6c UpdateInstance(Object+4, 0, 0), CBase::advise(true). Simple,
    // EquippedObj and the projectile branch make it pure again (0x005b8064). No caller reads
    // a result (0x004f4709, 0x005266ee, 0x0053ad5f, 0x00548c03, 0x0054b591 all go on without
    // looking at EAX).
    virtual void create(Archetype::Root* Arch);
    // 91 +0x16c pure in GameObject (RET 4): attach to the CObject with this id (Ship
    // FUN_005465d0, Solar FUN_00547760: CObject::Find(Id, Class), advise(true), connect).
    virtual bool find(unsigned const& Id) = 0;
    // 92 +0x170 FUN_00539740: resolve the archetype's explosion into ExplosionArch (+ preload),
    // FDUMP a warning when the archetype names one that does not exist, then join the watch list
    // when get_type & 0x107ffdff and bWatchListed.
    virtual void Slot170();
    // 93 +0x174 FUN_00539c60 (RET 4): Actions.update(Dt), then Slot1b8(Dt).
    virtual void update(float Dt);
    // 94 +0x178 FUN_00539810 (RET 4): the object's vt+0x64 (the physical radius about Center).
    virtual float Slot178(Vector& Center);
    // 95 +0x17c FUN_00539820 (RET 0xc): the LOD pick. Returns its first argument: a non-zero
    // low word in *CachedLod is copied straight to *OutLod (0x0053982c..0x0053983a), otherwise
    // the pick runs (Slot178 radius, camera vt+0x40, ...). The pick's port is the free function
    // GameobjPickLod in space_scene_obj_draw.cpp -- see client_gameobj.cpp.
    virtual unsigned* Slot17c(unsigned* OutLod, void* Camera, unsigned const* CachedLod);
    // 96 +0x180 LAB_00401e90: empty (EquippedObj overrides).
    virtual void Slot180();
    // 97 +0x184 FUN_00539b40 (RET 4): per-object lighting (FUN_005554d0). Body: the free
    // function GameobjUpdateDrawLightingRetail in space_scene_obj_draw.cpp.
    virtual void Slot184(void* Camera);
    // 98 +0x188 FUN_00539980 (RET 0x14): draw submit -- Slot180, Slot17c(&Lod, Camera,
    // &CachedLod), the LOD gate (a failed gate writes the LOD to *OutLod and returns),
    // the effect-list sweep, Slot184(Camera), the player flags, get_index, the envmap arm,
    // IRenderer2::Slot7(Camera, Inst, 1.0f, Flags, Matrix, Extra). Matrix = retail arg 4
    // (flags |= 0x120 when non-null), Extra = retail arg 5 (the IFLAppearance Slot8 out-param
    // when it was null). Body: GameobjSubmitDrawRetail in space_scene_obj_draw.cpp; the draw
    // drain calls this slot through the node LAB_0051f7a0.
    virtual unsigned* Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod, void* Matrix,
                              void* Extra);
    // 99 +0x18c LAB_00524e50 (RET 4): *OutB = DrawStateB; return DrawStateA.
    virtual unsigned Slot18c(unsigned* OutB);
    // 100 +0x190 LAB_00524e60: DrawStateA = DrawStateB = 0, Field_0x34 = 10.0f.
    virtual void Slot190();
    // 101 +0x194 FUN_00539c90 (RET 0x10): the collision pair --
    // Other->Slot1a0(A, D, this, B), then this->Slot1ac(A, B, Other, D).
    virtual void Slot194(void const* A, void const* B, FClientGameobj* Other, void const* D);
    // 102 +0x198 LAB_0053e4a0 (RET 4): empty. Simple FUN_005474a0 applies the root entry.
    virtual void Slot198(DamageList const& Damage);
    // 103 +0x19c FUN_0053a820 (RET 4): DestroyType 0 -> Slot15c(true); 1 -> Flags |= 4 and
    // Slot1b0(nullptr); anything else FDUMPs "Invalid DestroyType!".
    virtual void Slot19c(int DestroyType);
    // 104 +0x1a0 FUN_00539cd0 (RET 0x10): surface-hit effect (PickDistanceRecord 0x00407450,
    // FUN_00539f90). Contact points at two Vectors: the hit point (+0x00) and the surface
    // normal (+0x0c); B and D are not read.
    virtual void Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                         void const* D);
    // 105 +0x1a4 FUN_00539d80 (RET 0xc): munition-hit effect (0x00408a70, FUN_0053a230). The
    // munition gameobj is the THIRD argument: FUN_00539d80 reads its +0x10 object from
    // [ESP+0xc] (0x00539d84), and Munition's slot 101 pushes `this` last (0x0053ed94).
    virtual void Slot1a4(void const* A, void const* B, FClientGameobj* Munition);
    // 106 +0x1a8 LAB_0053f780 (RET 0xc): empty. The arguments are EquippedObj's
    // (FUN_005356c0): a gun hardpoint's sub-object id, the target object id and its sub-object.
    virtual void Slot1a8(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj);
    // 107 +0x1ac FUN_00539e70 (RET 0x10): multiplayer collision report through the server
    // interface DAT_0067ecd0 vt+0xdc.
    virtual void Slot1ac(void const* A, void const* B, FClientGameobj* Other, void const* D);
    // 108 +0x1b0 FUN_0053a880 (RET 4): explode at Pos (null = get_position()) through
    // FUN_005361e0 + Actions.add, or Slot15c(true) when there is no explosion.
    virtual void Slot1b0(Vector const* Pos);
    // 109 +0x1b4 FUN_0053a4c0 (RET 0xc): the hit sound. SourceId keys the per-source voice
    // pool at +0x80; SoundArch is an EffectArch's +0x18 (vt+0x30 tested first); Pos goes to the
    // voice's vt+0x8c. Caller: FUN_00539f90 (0x0053a21f).
    virtual void Slot1b4(unsigned SourceId, void* SoundArch, Vector const* Pos);
    // 110 +0x1b8 LAB_0053e4a0 (RET 4): empty.
    virtual void Slot1b8(float Dt);

    // ---- non-virtual members --------------------------------------------------------
    // FUN_0053a960: the cached attitude towards the player, refreshed through FUN_0045a490
    // every eighth call (AttitudeCountdown).
    int GetCachedAttitude();
    // FUN_0053a9c0 / FUN_0053a9d0 (callers 0x0051cf24 / 0x0051cfb5): read / write +0xa4.
    unsigned char GetFieldA4() const { return Field_0xa4; }
    void SetFieldA4(unsigned char Value) { Field_0xa4 = Value; }
    // FUN_0053a9e0 (callers 0x004d2763 .. 0x004d376a): the address of the font string at +0x48.
    FFontString* GetFontString48() { return &Field_0x48; }
    // FUN_0053a9f0 (callers 0x004d2756 / 0x004d2d9a): Flags bit 3, but only while +0x7c still
    // equals the surface width DAT_00610850; false otherwise.
    bool GetFlag3IfSurfaceWidthUnchanged() const;
    // FUN_0053aa10 (RET 4): Flags bit 3 = the argument; when it ends up set, +0x7c takes the
    // current surface width DAT_00610850.
    void SetFlag3(bool bValue);

    // FUN_00539f90 (RET 0x14): the surface-hit particle -- a new ParticleEffect for
    // EffectArch->VisEffect in one of the three HitEffects slots, attached at Pos / Normal in
    // object space -- then Slot1b4(SourceId, EffectArch->SndEffect, Pos). Skipped entirely for
    // the player's ship while SURFACE_HIT_EFFECTS (option 0x38) is off. The fifth argument
    // is never read ([ESP+0xb0] at that frame is not touched): slots 104/105 pass 0, the shield
    // wrapper's hit slots (0x0052e84d, 0x0052e9bd) pass the address of a zeroed local effect
    // companion, which therefore stays empty.
    // Callers: slot 104 (0x00539d67), slot 105 (0x00539e08), 0x0052e84d, 0x0052e9bd.
    void SpawnSurfaceHitEffect(unsigned SourceId, FEffectArch* EffectArch, Vector const* Pos,
                               Vector const* Normal, void* CompanionOut);
    // FUN_0053a230 (RET 0x14): the splash-hit one-shot -- EffectArch through FUN_00503070 at
    // Pos with the direction mirrored off Normal. Skipped for the player's ship while
    // SPLASH_HIT_EFFECTS (option 0x39) is off, and when the effect type fails the perf test
    // FUN_004fd570. Callers: slot 105 (0x00539e61), 0x0052ea3b.
    void SpawnSplashHitEffect(FEffectArch* EffectArch, Vector const* Pos, Vector const* Normal,
                              Vector const* Velocity, void* CompanionOut);

    CObject* GetObject() const { return Object; }

    // FUN_005397f0 (thiscall, RET 4) -- the object narrowed to a CSimple ((class & 3) == 3, else
    // null, used with no test), then a tail JMP to CSimple::SetOwnerPlayer(PlayerId). Callers:
    // the IClient system-switch-out and land handlers (0x0053d60e, 0x0053d794), 0x00548205.
    void SetOwnerPlayer(unsigned PlayerId);

    // IObjAffect -- GameObject's own table, .rdata:005de76c (FUN_005392d0 stores it at +4,
    // .text:005393ac). Every entry is a retail error stub with the same RET n / value pattern
    // as Server.dll GameObject's; see client_gameobj.cpp. Simple and Solar keep these entries,
    // EquippedObj overrides six and Ship nineteen (see those classes).
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

    // ---- data (IObjInspectImpl ends at +0x14) ---------------------------------------
    FClientGameobj* ListNext;          // +0x14  ObjDB bucket link (FUN_00542350 / 00542390)
    FClientGameobj* ListPrev;          // +0x18
    int ManagerKind;                   // +0x1c  ObjDB bucket (1 ship, 2 solar, 3 beam, ...)
    ActionDB Actions;                  // +0x20  update(Dt) by slot 93, add by slot 108
    FGameobjEffectList EffectList;     // +0x28
    float Field_0x34;                  // +0x34  ctor 0; slot 100 sets 10.0f; Ship ctor rand()/32768
    unsigned DrawStateA;               // +0x38  slot 99 / slot 100; lighting FUN_005554d0 arg
    unsigned DrawStateB;               // +0x3c
    Archetype::Explosion* ExplosionArch; // +0x40 slot 92 resolves; the dtor releases
    unsigned char Flags;               // +0x44  bit0 slot 86/87, bit1 slot 88/89, bit2 slot 85
    unsigned char Pad45[3];
    FFontString Field_0x48;            // +0x48  FUN_004144b0; FUN_0053a9e0 returns its address
    int Field_0x7c;                    // +0x7c  surface width DAT_00610850 (FUN_0053aa10 / 0053a9f0)
    unsigned char SoundAllocatorByte;  // +0x80  the ctor copies the kind's low byte here
    unsigned char Pad81[3];
    FGameobjSound* SoundsBegin;        // +0x84
    FGameobjSound* SoundsEnd;          // +0x88
    FGameobjSound* SoundsCap;          // +0x8c
    FParticleEffect* HitEffects[3];    // +0x90  FUN_00539f90 fills (DAT_00613e60 = 3)
    int CachedAttitude;                // +0x9c  FUN_0053a960
    int AttitudeCountdown;             // +0xa0  ctor -1
    unsigned char Field_0xa4;          // +0xa4  FUN_0053a9c0 get / FUN_0053a9d0 set
    unsigned char Pada5[3];
    // +0xa8 -- read by slot 92 (FUN_00539740) and the HUD; written by Simple's ctor
    // (FUN_00547440, 0) and FUN_00547550. GameObject's own ctor leaves it alone.
    unsigned char bWatchListed;        // +0xa8
    unsigned char Pada9[3];
};

static_assert(offsetof(FClientGameobj, ListNext) == 0x14, "Gameobj ListNext +0x14");
static_assert(offsetof(FClientGameobj, ListPrev) == 0x18, "Gameobj ListPrev +0x18");
static_assert(offsetof(FClientGameobj, ManagerKind) == 0x1c, "Gameobj ManagerKind +0x1c");
static_assert(offsetof(FClientGameobj, Actions) == 0x20, "Gameobj ActionDB +0x20");
static_assert(offsetof(FClientGameobj, EffectList) == 0x28, "Gameobj effect list +0x28");
static_assert(offsetof(FClientGameobj, Field_0x34) == 0x34, "Gameobj +0x34");
static_assert(offsetof(FClientGameobj, DrawStateA) == 0x38, "Gameobj +0x38");
static_assert(offsetof(FClientGameobj, DrawStateB) == 0x3c, "Gameobj +0x3c");
static_assert(offsetof(FClientGameobj, ExplosionArch) == 0x40, "Gameobj explosion +0x40");
static_assert(offsetof(FClientGameobj, Flags) == 0x44, "Gameobj flags +0x44");
static_assert(offsetof(FClientGameobj, Field_0x48) == 0x48, "Gameobj font string +0x48");
static_assert(offsetof(FClientGameobj, Field_0x7c) == 0x7c, "Gameobj +0x7c");
static_assert(offsetof(FClientGameobj, SoundsBegin) == 0x84, "Gameobj sounds +0x84");
static_assert(offsetof(FClientGameobj, HitEffects) == 0x90, "Gameobj hit effects +0x90");
static_assert(offsetof(FClientGameobj, CachedAttitude) == 0x9c, "Gameobj attitude +0x9c");
static_assert(offsetof(FClientGameobj, AttitudeCountdown) == 0xa0, "Gameobj countdown +0xa0");
static_assert(offsetof(FClientGameobj, Field_0xa4) == 0xa4, "Gameobj +0xa4");
static_assert(offsetof(FClientGameobj, bWatchListed) == 0xa8, "Gameobj +0xa8");
static_assert(sizeof(FClientGameobj) == 0xac, "GameObject sizeof 0xac");

// Simple : GameObject -- FUN_00547440. Adds no data; its ctor zeroes bWatchListed.
class FClientSimpleGameobj : public FClientGameobj
{
public:
    explicit FClientSimpleGameobj(EClientGameobjKind Kind);
    // FUN_00547470 is the deleting wrapper; the body is GameObject's (thunk to FUN_005394b0).
    ~FClientSimpleGameobj() override;

    // 90 +0x168: pure again in Simple (0x005b8064).
    void create(Archetype::Root* Arch) override = 0;
    // 102 +0x198 FUN_005474a0 (RET 4): multiplayer only -- the root entry's hit points go to
    // CSimple::set_hit_pts (vt+0x98).
    void Slot198(DamageList const& Damage) override;
    // 103 +0x19c FUN_00547500 (RET 4): CSimple::set_hit_pts(0), then GameObject's slot 103.
    void Slot19c(int DestroyType) override;

    // 111 +0x1bc LAB_00547530: the CSimple's TypeBits (+0xe0).
    virtual unsigned Slot1bc() const;

    // FUN_00547550 (RET 8): bWatchListed = (bWatch == 1), then join or leave the watch list.
    // The first argument is not read.
    void SetWatchListed(int Unused, int bWatch);
};
static_assert(sizeof(FClientSimpleGameobj) == 0xac, "Simple sizeof 0xac");

// EquippedObj : Simple, INotify -- FUN_00532bc0. INotify's vptr lands at +0xac (right after
// Simple), the way retail's ctor stores 005de1a8 there.
class FClientEquippedObj : public FClientSimpleGameobj, public INotify
{
public:
    explicit FClientEquippedObj(EClientGameobjKind Kind);
    // FUN_00532d40; deleting wrapper FUN_00532d00.
    ~FClientEquippedObj() override;

    // INotify -- FUN_00534730.
    void notify(Event Code, void* Param) override;

    // IObjInspect slot 14 (+0x38) LAB_00534860: `MOV EAX,[ECX]; JMP [EAX+0x1c8]` -- the object
    // is dying while a death fuse on it still burns (Slot1c8, virtual). Slot 14 of the
    // EquippedObj (.rdata:005de274), Ship (005dfda4) and Solar (005e033c) vtables.
    bool is_dying() const override;
    // IObjInspect slot 72 (+0x120) LAB_00535650 (RET 8): Common's scan_cargo, then -- for the
    // player's ship -- IServer::SPScanCargo(this id, the scanned object's id, the player's
    // connection). Slot 72 of the same three vtables (005de35c / 005dfe8c / 005e0424).
    int scan_cargo(IObjRW* Scanned, EquipDescVector& OutCargo) const override;

    // 90 +0x168: pure (0x005b8064).
    void create(Archetype::Root* Arch) override = 0;
    // 92 +0x170 FUN_00533e10.
    void Slot170() override;
    // 93 +0x174 FUN_00534010.
    void update(float Dt) override;
    // 95 +0x17c FUN_00533710.
    unsigned* Slot17c(unsigned* OutLod, void* Camera, unsigned const* CachedLod) override;
    // 96 +0x180 LAB_0051b480.
    void Slot180() override;
    // 98 +0x188 FUN_00533780.
    unsigned* Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod, void* Matrix,
                      void* Extra) override;
    // 102 +0x198 FUN_005339e0.
    void Slot198(DamageList const& Damage) override;
    // 104 +0x1a0 FUN_005340b0.
    void Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00534190.
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 106 +0x1a8 FUN_005356c0 (RET 0xc): SubTargets[SubObj] = {TargetId, TargetSubObj}.
    void Slot1a8(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj) override;

    // FUN_005357f0 (RET 4): re-aim the gun at SubObj on the target SubTargets holds for it
    // (inserting the {0, SUBOBJ_ID_NONE} default when there is none), through IObjAffect slot 17
    // when the gun's -- or, for SUBOBJ_ID_NONE, this object's -- target differs. Caller: the
    // multiplayer arm of slot_34 (0x00534bf6).
    void ApplySubTarget(unsigned short SubObj);
    // 108 +0x1b0 FUN_00533be0.
    void Slot1b0(Vector const* Pos) override;

    // 112 +0x1c0 FUN_00534240 (RET 0x14): ignite the fuse FuseId on this object -- the first
    // burning fuse of that name on the +0xc4 list gets Fuse::IgniteAt(SourceId, SubObjId,
    // AtT); otherwise a copy of the ClientFuseDB prototype is appended, bound to this object,
    // randomised, given Lifetime unless it is -1, and ignited. False only when the fuse is
    // not in the database (FDUMP). Callers: the equip-FX rebuild FUN_00533e10 (0x00533ee7,
    // 0x00533f92), BURNFUSE FUN_0053e090, the ship override FUN_00545a90. Body:
    // EquippedObjIgniteFuse (client_equip_fx.cpp).
    virtual bool Slot1c0(unsigned SourceId, ID_String const* FuseId, unsigned short SubObjId,
                         float AtT, float Lifetime);
    // 113 +0x1c4 FUN_005343d0 (RET 0xc): the first fuse named FuseId on the +0xc4 list gets
    // Fuse::UnBurn(SubObjId, AtT); true when one did.
    virtual bool Slot1c4(ID_String const* FuseId, unsigned short SubObjId, float AtT);
    // 114 +0x1c8 FUN_00534420 (bare RET): true when a death fuse (Fuse +0xc) on the +0xc4 list
    // is not yet spent (Fuse::IsSpent).
    virtual bool Slot1c8();
    // 115 +0x1cc FUN_00534460 (RET 4): the equipment message -- 0x18 fire, 0x19 activate.
    // Port: EquippedObjHandleEquipMsg (cli_projectile_fx.cpp).
    virtual unsigned char Slot1cc(FCliEquipFirePacket const* Packet);

    // FUN_00533d10 (RET 0xc): a part that came off becomes a Debris gameobj (its [Debris]
    // record by DebrisType, the default with a warning when there is none), the part's effects
    // move to it (FUN_004fd230), and a named Explosion (null = none) starts on it at this
    // Gameobj's position. Callers: notify 5 and 7 (0x00534783 .. 0x00534824).
    void SpawnPartDebris(CObject* PartObject, ID_String const* DebrisType,
                         ID_String const* Explosion);

    // IObjAffect overrides (.rdata:005de1bc): slots 13, 15, 16, 17, 19, 22.
    int slot_34(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                int SourceClient) override;                                     // 00534870
    int slot_3c(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                int SourceClient) override;                                     // 00535010
    int slot_40(unsigned short const* EquipId, IObjRW* Target, int SourceClient) override; // 00534e60
    int slot_44(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                int SourceClient) override;                                     // 005351d0
    int slot_4c(unsigned short const* EquipId, bool bActivate, int SourceClient) override; // 00535330
    int slot_58(unsigned short EquipId, unsigned Count, int SourceClient) override; // 00535470

    // ---- data -------------------------------------------------------------------------
    // (+0xac is INotify's vptr.)
    unsigned char EquipFlags;          // +0xb0  the ctor clears bit 0 only
    unsigned char Padb1[3];
    // +0xb4 .. +0xbf: std::list of equipment-effect wrappers, node 0xc {next, prev, wrapper}
    // (client_equip_fx.cpp owns the wrappers) -- a RAII member (FEquipFxList).
    FEquipFxList EquipFx;              // +0xb4 (allocator byte), +0xb8 head, +0xbc count
    // +0xc0 .. +0xcb: std::list of burning fuses, node 0x40 (FFuseBurnNode, client_fuse.h) --
    // a RAII member whose destructor is FUN_005359c0 (FEquippedFuseList).
    FEquippedFuseList Fuses;           // +0xc0 (allocator byte), +0xc4 head, +0xc8 count
    // +0xcc .. +0xdf: retail's VC6 std::map<unsigned short, FEquipSubTarget> (comparator and
    // allocator bytes, head, nil, multi flag, size; nodes 0x1c with the key word at +0x0c and
    // the value at +0x10) -- the target each gun hardpoint was last told to track, written by
    // slot 106 (FUN_005356c0) and applied by FUN_005357f0. The port keeps a std::map of its own
    // in the same 0x14 bytes (no retail ABI).
    std::map<unsigned short, FEquipSubTarget> SubTargets; // +0xcc
    unsigned char PadSubTargets[0x14 - sizeof(std::map<unsigned short, FEquipSubTarget>)];
    unsigned char Field_0xe0;          // +0xe0
    unsigned char Pade1[3];
};
static_assert(offsetof(FClientEquippedObj, EquipFlags) == 0xb0, "EquippedObj +0xb0");
static_assert(offsetof(FClientEquippedObj, EquipFx) == 0xb4, "EquippedObj equip-FX list +0xb4");
static_assert(offsetof(FClientEquippedObj, Fuses) == 0xc0, "EquippedObj fuse list +0xc0");
static_assert(offsetof(FClientEquippedObj, SubTargets) == 0xcc, "EquippedObj sub-target map +0xcc");
static_assert(offsetof(FClientEquippedObj, Field_0xe0) == 0xe0, "EquippedObj +0xe0");
static_assert(sizeof(FClientEquippedObj) == 0xe4, "EquippedObj sizeof 0xe4");

class FClientSolarGameobj : public FClientEquippedObj
{
public:
    // FUN_005475b0.
    FClientSolarGameobj();
    // FUN_00547680; deleting wrapper FUN_00547650.
    ~FClientSolarGameobj() override;

    // INotify -- FUN_005478f0.
    void notify(Event Code, void* Param) override;

    // 90 +0x168 FUN_005477d0: FUN_00539bc0(0x303, Arch), open, CEqObj::connect(INotify*).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_00547760.
    bool find(unsigned const& Id) override;
    // 92 +0x170 FUN_00547850.
    void Slot170() override;
    // 93 +0x174 FUN_00547880.
    void update(float Dt) override;
    // 94 +0x178 FUN_00548110.
    float Slot178(Vector& Center) override;
    // 98 +0x188 FUN_00548220.
    unsigned* Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod, void* Matrix,
                      void* Extra) override;
    // 104 +0x1a0 FUN_00548520.
    void Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00548570.
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 108 +0x1b0 FUN_005485b0.
    void Slot1b0(Vector const* Pos) override;

    // FUN_005485c0 (thiscall, RET 4) -- the client half of an [Object] section, read after the
    // spawn FUN_00548a20 rewound the reader (0x00548c73 set_state, call at 0x00548c7c): ring,
    // burn_color, dock_script, voice, jump_effect, star; then the nebula at the position.
    void ReadIni(INI_Reader& Reader);

    // ---- data -------------------------------------------------------------------------
    // +0xe4 -- the star (StarEffects::Star, FStarSphere, vtable 005db658) a sun solar gets
    // from its "star" key (FUN_0050c630 at 0x0054880d, ReadIni); the update ticks it through
    // its slot 2 (0x005478db). space_scene_obj_draw.cpp tests it.
    FStarSphere* Field_0xe4;           // +0xe4  ctor 0
    // +0xe8 -- "dock_script" (ReadIni 0x0054872c..0x00548783, the inlined TString assign). The
    // ctor zeroes the length and the first char only.
    TString<0x104> DockScript;         // +0xe8 .. +0x1ef
    unsigned char Field_0x1f0;         // +0x1f0 ctor 1
    unsigned char Pad1f1[3];
    float BurnColor[3];                // +0x1f4 ctor 1.0f x3; "burn_color" / 255 (ReadIni)
    ID_String Field_0x200;             // +0x200 ctor ID_String::clear; "jump_effect" CreateID
    void* Field_0x204;                 // +0x204 ctor 0; the dtor runs FUN_00502e40 and frees it
};
static_assert(offsetof(FClientSolarGameobj, DockScript) == 0xe8, "Solar +0xe8 dock_script");
static_assert(offsetof(FClientSolarGameobj, BurnColor) == 0x1f4, "Solar +0x1f4 burn_color");
static_assert(offsetof(FClientSolarGameobj, Field_0x1f0) == 0x1f0, "Solar +0x1f0");
static_assert(offsetof(FClientSolarGameobj, Field_0x200) == 0x200, "Solar +0x200");
static_assert(sizeof(FClientSolarGameobj) == 0x208, "Solar sizeof 0x208");

class FClientShipGameobj : public FClientEquippedObj
{
public:
    // FUN_00543380.
    FClientShipGameobj();
    // FUN_005444a0; deleting wrapper FUN_00543500.
    ~FClientShipGameobj() override;

    // INotify -- FUN_00545670.
    void notify(Event Code, void* Param) override;

    // 90 +0x168 LAB_00546550: FUN_00539bc0(0x503, Arch), open, CEqObj::connect(INotify*).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_005465d0.
    bool find(unsigned const& Id) override;
    // 92 +0x170 FUN_005447b0.
    void Slot170() override;
    // 93 +0x174 FUN_00545050.
    void update(float Dt) override;
    // 97 +0x184 FUN_00546480.
    void Slot184(void* Camera) override;
    // 98 +0x188 FUN_00546050.
    unsigned* Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod, void* Matrix,
                      void* Extra) override;
    // 102 +0x198 FUN_00545f50.
    void Slot198(DamageList const& Damage) override;
    // 104 +0x1a0 FUN_00545e90.
    void Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00545ed0.
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 108 +0x1b0 FUN_00545d80.
    void Slot1b0(Vector const* Pos) override;
    // 110 +0x1b8 FUN_005455b0.
    void Slot1b8(float Dt) override;
    // 112 +0x1c0 FUN_00545a90 (RET 0x14): EquippedObj's ignite (FUN_00534240) and, when it
    // lit DEATH_COMM_FUSE, the formation / follow followers' behaviours are told (event 7 with
    // this ship's id) and the player drops this ship as its target.
    bool Slot1c0(unsigned SourceId, ID_String const* FuseId, unsigned short SubObjId, float AtT,
                 float Lifetime) override;
    // 115 +0x1cc FUN_00544d70 (RET 4). Port: CliShipHandleEquipUpdate (cli_projectile_fx.cpp).
    unsigned char Slot1cc(FCliEquipFirePacket const* Packet) override;
    // 116 +0x1d0 LAB_00545cc0 (bare RET).
    virtual void Slot1d0();

    // IObjAffect overrides (.rdata:005dfcec).
    int set_throttle(float Throttle) override;                               // 00546640
    int set_axis_throttle(Vector const& AxisThrottle) override;              // 00546680
    int set_nudge_vec(Vector const& NudgeVec) override;                      // 005466c0
    int set_strafe_dir(StrafeDir Dir) override;                              // 00546700
    int set_formation_leader(IObjRW* Leader) override;                       // 00547140
    int set_formation_offset(Vector const& Offset) override;                 // 005470f0
    int add_formation_follower(IObjRW* Follower) override;                   // 00547190
    int remove_formation_follower(IObjRW* Follower) override;                // 005471c0
    int slot_38(unsigned short EquipId, int Count, int SourceClient) override; // 005467e0
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override; // 005468e0
    int slot_50(bool bEngage, bool bArg2, int SourceClient) override;        // 00546a10
    int slot_54(bool bActivate, int SourceClient) override;                  // 00546b50
    int slot_5c(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                int bFlag, RequestCB* Callback) override;                   // 00546df0
    int slot_60(int RequestType, unsigned const* TargetId, int Arg3) override; // 00546e40
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     bool bFastExit, float Speed) override;                  // 00546e90
    int stop_tradelane() override;                                           // 00546f80
    int slot_6c(int ArgA, int ArgB) override;                                // 00547250
    int slot_70(unsigned Id) override;                                       // 005471f0
    int slot_74(unsigned Arg) override;                                      // 00547220

    // FUN_00543d50 (__thiscall, RET 8) -- the client side of a launch, called by the IClient
    // launch handler (LAB_0053d7e0 at 0x0053d877) with the dock gameobj found in bucket 0 and
    // the launch-point index. Only the player's character launches (get_id vs DAT_00673378):
    // CShip::launch, seven seconds of suppressed player collisions (FUN_005544d0), and -- when
    // this is the watched player ship -- the docking camera placed on the dock's
    // "HPLaunchCam<A+Index>" hardpoint, else "HPDockCam<A+Index>", else the player camera.
    void LaunchFromDock(FClientGameobj* DockGo, int LaunchIndex);

    // FUN_00543b40 (__thiscall, RET 8) -- the client side of a landing, called by the IClient
    // land handler FUN_0053d720 (0x0053d7cf) with the dock gameobj (null when it is not in the
    // ObjDB) and the base id. Flag bit 3 of +0x18c; with a dock, an Action_Land
    // (client_land_action.h), the axis throttle zeroed and, for the watched player ship, the
    // docking camera on the dock's "HPDockCam" 'A' hardpoint and its three-second fade. Without
    // a dock the player's ship asks for the base straight away and cuts the screen to black;
    // any other ship goes through slot 87 with true.
    void RequestLand(FClientGameobj* DockGo, unsigned const* BaseId);

    // FUN_005437d0 (__thiscall, RET 4) -- the jump state machine: 0 idle, 1 jump out (the
    // Action_JumpOut swing), 2 into the tunnel, 3 system switch requested, 4 jump in, 5 done.
    // Each state runs its entry work (and the JumpShipEffect's hook); any other value warns
    // (ship.cpp line 0x272). The state lands in +0x158 and the effect's clock (+0x70) restarts.
    void SetJumpState(int State);
    // FUN_00543530 (__thiscall, RET 4) -- the jump out through Gate (the gate / hole solar
    // gameobj): a fresh JumpShipEffect for it at +0x188, state 1, and an Action_JumpOut over the
    // effect's JumpOutTime. Caller: the IClient system-switch-out handler (0x0053d66e).
    void BeginJumpOut(FClientGameobj* Gate);
    // FUN_00543630 (__thiscall, RET 8) -- the jump in at Pos / Orient: +0x15c = 1; the player's
    // ship stores them in its effect and raises the camera cut, any other ship gets a fresh
    // no-gate JumpShipEffect with them and goes to state 4. Caller: the IClient system-switch-in
    // handler (0x0053d70f).
    void BeginJumpIn(Vector const* Pos, Matrix const* Orient);
    // FUN_00543750 (__thiscall on the ship, RET 4; `this` is never read) -- Gate narrowed to a
    // CSolar ((class & 0x303) == 0x303), its DestSystem (+0x1b4) stored as the pending jump
    // system (FUN_00548ed0, flag 0). Caller: the system-switch-out handler (0x0053d65d) for a
    // gate type with neither bit 0x40 nor 0x800.
    void RequestSystemSwitch(FClientGameobj* Gate);
    // FUN_005437a0 (__thiscall, RET 8) -- the object narrowed to a CShip ((class & 0x503) ==
    // 0x503), then a tail JMP to CShip::airlock_in(Pos, Orient). Caller: the system-switch-in
    // handler (0x0053d6d5).
    void AirlockIn(Vector const& Pos, Matrix const& Orient);
    // FUN_00544380 (__thiscall, RET 4) -- the pilot mesh: with a non-empty PilotMesh and an
    // "HpPilot" hardpoint on the ship, the simple archetype PilotMesh names goes to +0x104
    // (load, companion Load(0), create_instance -> +0x10c), and +0x118 becomes {HpPilot's
    // orientation, HpPilot's position minus the pilot mesh's "HpConnect" position}. Caller:
    // slot 92 (FUN_005447b0 at 0x00544801) with the ship companion's pilot_mesh (+0x64).
    void AttachPilotMesh(ID_String const* PilotMesh);
    // FUN_005449f0 (__thiscall, RET 4; the pushed connection id is never read) -- the next
    // target: every object in the ship's scanner list (CEquipManager::FindFirst(0x80000),
    // ScanList +0x24 / count +0x824) that the CShip calls an enemy and that is not dying; a
    // CSimple also needs TypeBits & 0x7f0000 or a watch-listed gameobj (+0xa8). The nearest
    // watch-listed one wins, else the nearest of all; null without a scanner. Caller: HUD_Target
    // event 0x26 (0x004e2a6b).
    IObjRW* PickNextTarget(int ConnectionId);
    // FUN_00544b90 (__thiscall, RET 8) -- the player's target: IObjAffect set_target (vt+0x48)
    // with the player's connection, then every gun (CEquipTraverser(0x80)) re-aimed at it
    // through FUN_005351d0 (FClientEquippedObj::slot_44, called directly). Callers: HUD_Target
    // events 0x24 / 0x25 / 0x26.
    void SetTargetWithGuns(IObjRW* Target, unsigned short SubTarget);
    // FUN_00544c30 (__thiscall, RET 4) -- the tractor: false without a tractor
    // (FindFirst(0x100000)); with a Target and a tractor that is not destroyed, a CLoot target
    // ((class & 0x803) == 0x803) gets IObjAffect vt+0x40; otherwise vt+0x4c with false. True
    // whenever there is a tractor. Caller: HUD_Target event 0x8b (0x004e2ab5).
    bool RequestTractor(IObjRW* Target);
    // FUN_00546d00 (__thiscall, RET 0xc) -- the answer to the request slot_5c sent: the pending
    // callback (+0xec) hears (Result, Code, Extra) and is dropped when it answers false (no
    // callback: "stale request detected"); the player's ship then plays the docking voice line
    // for Result 0 / 2 and Code 0..5. Caller: the IClient REQUEST_RETURNED handler LAB_0053bf80
    // (0x0053bfa4), on whatever gameobj the id names.
    void RequestReturned(int Result, int Code, int Extra);
    // FUN_00543f50 (__thiscall, bare RET) -- SND_CARGO_JETTISONED at the hull's HpBaySurface
    // hardpoint (the bay-door sequence of the notify, case 2, with that sound). Caller: the
    // CREATELOOT handler FUN_0053b910 (0x0053bc5f) for a kind-1 request, on the owner's gameobj.
    void PlayCargoJettisonSound();

    // ---- data (FUN_00543380 zeroes all of it; names not recovered yet) ----------------
    // +0xe4 / +0xe8 -- a countdown and a count read by the update (FUN_00545050,
    // 0x0054515c..0x005451d9): while +0xe4 is non-zero it runs down by Dt; once it reaches 0,
    // CShip::close_bay runs +0xe8 times and both return to 0. The writer is the server arm of
    // IObjAffect slot 14 (FUN_005467e0, 0x005468ba..0x005468cd): one more close per jettison,
    // the countdown += open_bay's result + CShip::JETTISON_CLEARANCE_TIME.
    float Field_0xe4;                  // +0xe4
    int Field_0xe8;                    // +0xe8
    RequestCB* Field_0xec;             // +0xec  slot_5c (00546df4) stores its callback, slot_60 clears
    ISoundSource* Field_0xf0;          // +0xf0  the dtor stops (vt+0x60) and releases (vt+8) it
    // +0xf4 .. +0x103 -- a 0x10 effect companion (arch, sound, particle, light): the dtor
    // releases +0xfc (FUN_004f7a90), +0xf8 (vt+8), +0x100 (FUN_00537ad0), then zeroes +0xf4.
    void* Field_0xf4[4];
    Archetype::Root* Field_0x104;      // +0x104 the dtor releases it (0x00544501)
    FMeshHolder Field_0x108;           // +0x108 FUN_00420d40 (the dtor runs FUN_00420f70)
    Transform Field_0x118;             // +0x118 identity orientation, zero position
    Archetype::ISound* Field_0x148;     // +0x148 fire-failed sound archetype
    ISoundSource* Field_0x14c;         // +0x14c the dtor stops and releases it
    float Field_0x150;                 // +0x150 the update runs it down by Dt while above 0
    unsigned char Field_0x154;         // +0x154
    unsigned char Pad155[3];
    // +0x158 -- the state FUN_005437d0 (switch 0..5) stores last (0x00543b12). Readers: the
    // draw pass (0x0051c221 compares 2 and 3), StarSys music (0x005550c4), 0x0054380d (== 5).
    int Field_0x158;
    unsigned char Field_0x15c;         // +0x15c FUN_00543630 sets 1 (0x0054364b)
    unsigned char Pad15d[3];
    // +0x160 .. +0x183 -- one 0x24-byte member whose destructor is FUN_00544710 (inlined in the
    // Ship dtor at 0x00544613..0x0054468c): a dword, then two 0x10 effect companions. They are
    // the tradelane ring FX binders tradelane_fx.cpp reaches by offset (+0x164 / +0x174). The
    // earlier "0xdc-byte object at +0x17c" was the dtor's ESI-relative +0x17c misread: the Ship
    // dtor runs with ESI = this + 0xc, so that object is +0x188.
    unsigned Field_0x160;              // +0x160
    void* Field_0x164[4];              // +0x164 companion (arch, sound, particle, light)
    void* Field_0x174[4];              // +0x174 companion
    unsigned Field_0x184;              // +0x184 (tradelane_fx.cpp reads bytes +0x184/+0x185)
    // +0x188 -- a 0xdc-byte object built by FUN_00543530 (FUN_00503440(this, Arg)) or
    // FUN_00543630 (FUN_005038c0(this)), destroyed by FUN_00503a80 + free (FUN_005437d0 case 0,
    // FUN_00543530, FUN_00543630, the dtor). FUN_005437d0's tail clears its +0x70.
    void* Field_0x188;
    // +0x18c -- flag byte: bit 0 / bit 1 set by slot 105 (FUN_00545ed0, 0x00545f07), both
    // cleared by slot 92 (FUN_005447b0, 0x005447c6), bit 3 set by FUN_00543b40 (0x00543b71).
    // The ctor clears bits 0..1 and then stores 0 over the whole dword (0x00543498/0x005434a5).
    unsigned char Field_0x18c;
    unsigned char Pad18d[3];
};
static_assert(offsetof(FClientShipGameobj, Field_0xec) == 0xec, "Ship +0xec");
static_assert(offsetof(FClientShipGameobj, Field_0xf0) == 0xf0, "Ship +0xf0");
static_assert(offsetof(FClientShipGameobj, Field_0x104) == 0x104, "Ship +0x104");
static_assert(offsetof(FClientShipGameobj, Field_0x108) == 0x108, "Ship mesh holder +0x108");
static_assert(offsetof(FClientShipGameobj, Field_0x118) == 0x118, "Ship transform +0x118");
static_assert(offsetof(FClientShipGameobj, Field_0x148) == 0x148, "Ship +0x148");
static_assert(offsetof(FClientShipGameobj, Field_0x158) == 0x158, "Ship +0x158");
static_assert(offsetof(FClientShipGameobj, Field_0x160) == 0x160, "Ship +0x160");
static_assert(offsetof(FClientShipGameobj, Field_0x164) == 0x164, "Ship companion +0x164");
static_assert(offsetof(FClientShipGameobj, Field_0x174) == 0x174, "Ship companion +0x174");
static_assert(offsetof(FClientShipGameobj, Field_0x184) == 0x184, "Ship +0x184");
static_assert(offsetof(FClientShipGameobj, Field_0x188) == 0x188, "Ship +0x188");
static_assert(offsetof(FClientShipGameobj, Field_0x18c) == 0x18c, "Ship +0x18c");
static_assert(sizeof(FClientShipGameobj) == 0x190, "Ship sizeof 0x190 (operator_new(400))");

// ===========================================================================
// The projectile branch: Projectile : Simple, Munition : Projectile, Beam : Munition.
//   Projectile  ctor FUN_00542550(kind)  0xc0  IObjRW 005df95c, IObjAffect 005df8dc,
//                                             CBase 005df8d0 -> FUN_005425c0
//   Munition    ctor FUN_0052e100(kind)  0xc0  IObjRW 005dd8cc, IObjAffect 005dd84c,
//                                             CBase 005dd844 -> FUN_0053d440
//   Beam        ctor inlined in FUN_0052dde0 (0x0052de9f..0x0052ded2)
//                                       0xd4  IObjRW 005dd67c, IObjAffect 005dd5fc,
//                                             CBase 005dd5f4 -> FUN_0052e140
//   Mine        ctor inlined in FUN_0053ca40 (0x0053ca8d..0x0053caa4): FUN_00542550(5) and the
//                                       0xc0  IObjRW 005dedcc, IObjAffect 005ded4c,
//                                             CBase 005ded44 -> FUN_0053d440
//   CounterMeasure  inlined in FUN_0053cd70 (0x0053cdbd..0x0053cdd4): FUN_00542550(6)
//                                       0xc0  IObjRW 005df01c, IObjAffect 005def9c,
//                                             CBase 005def94 -> FUN_0053d440
//   Guided      inlined in FUN_0053d080 (0x0053d0cd..0x0053d0e4): FUN_00542550(4)
//                                       0xc0  IObjRW 005df26c, IObjAffect 005df1ec,
//                                             CBase 005df1e4 -> FUN_0053d440
// RTTI (COL at vftable[-4]): Mine 005eef54 and CounterMeasure 005eefe4 are Projectile's
// children, Guided 005ef074 is Munition's. The three add no data (operator new(0xc0), the
// Projectile size) and share the deleting destructor FUN_0053d440 (Projectile's body). IObjAffect
// is Simple's table unchanged in all of them except Guided's slot 18 (LAB_0053eed0).
// ===========================================================================

class FClientProjectileGameobj : public FClientSimpleGameobj
{
public:
    // FUN_00542550 (RET 4): Simple's ctor, ConstEffectDelay = -1.0f, the companion zeroed.
    explicit FClientProjectileGameobj(EClientGameobjKind Kind);
    // FUN_005425f0 (this = the CBase sub-object); deleting wrapper FUN_005425c0.
    ~FClientProjectileGameobj() override;

    // 90 +0x168: pure (0x005b8064).
    void create(Archetype::Root* Arch) override = 0;
    // 92 +0x170 FUN_005426e0: GameObject's slot 92, then the munition companion's const_effect
    // -- started now through FUN_00503070 when its delay is 0, else the delay is armed.
    void Slot170() override;
    // 93 +0x174 FUN_00542790 (RET 4): unless done, GameObject's update, then the armed delay
    // counts down and, on reaching 0, starts the const_effect.
    void update(float Dt) override;
    // 104 +0x1a0 FUN_0053d4f0: RET 0x10, empty.
    void Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 108 +0x1b0 FUN_00542870 (RET 4): drop the companion's node from the effect list
    // (FUN_004fcef0), then GameObject's slot 108.
    void Slot1b0(Vector const* Pos) override;
    // 112 +0x1c0 LAB_005428a0 (RET 4): *Out = identity rotation, zero translation.
    virtual void Slot1c0(Transform* Out);

    float ConstEffectDelay;     // +0xac  -1.0f = not armed
    void* OneShotCompanion[4];  // +0xb0  {EffectArch, ISoundSource*, particle, light}
};
static_assert(offsetof(FClientProjectileGameobj, ConstEffectDelay) == 0xac, "Projectile +0xac");
static_assert(offsetof(FClientProjectileGameobj, OneShotCompanion) == 0xb0, "Projectile +0xb0");
static_assert(sizeof(FClientProjectileGameobj) == 0xc0, "Projectile sizeof 0xc0");

class FClientMunitionGameobj : public FClientProjectileGameobj
{
public:
    // FUN_0052e100 (RET 4): Projectile's ctor with the kind, then Munition's vtables.
    explicit FClientMunitionGameobj(EClientGameobjKind Kind);
    // Projectile's body (the thunk FUN_0053d470); deleting wrapper FUN_0053d440.
    ~FClientMunitionGameobj() override;

    // 101 +0x194 FUN_0053ed80 (RET 0x10): Other->Slot1a4(A, D, this), then
    // this->Slot1ac(A, B, Other, D).
    void Slot194(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
};
static_assert(sizeof(FClientMunitionGameobj) == 0xc0, "Munition sizeof 0xc0");

struct FBeamEffect; // beam_effect.h

class FClientBeamGameobj : public FClientMunitionGameobj
{
public:
    // 0x0052de9f..0x0052ded2: Munition's ctor with kind 3, FUN_005154a0 on +0xc0 with four
    // nulls, +0xd0 = 0, Beam's vtables.
    FClientBeamGameobj();
    // FUN_0053f1e0 (this = the CBase sub-object); deleting wrapper FUN_0052e140.
    ~FClientBeamGameobj() override;

    // 90 +0x168 FUN_0053f2f0 (RET 4): FUN_00539bc0(0x67, Arch), then the CBeam's vt+0x4c
    // open(Arch) with no null test.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053f330: XOR AL,AL / RET 4.
    bool find(unsigned const& Id) override;
    // 93 +0x174 LAB_0053f610 (RET 4): Projectile's update, the object's vt+0x50
    // update(Dt, CObject::UPDATE_DEFAULT), then Slot15c(true) once the object's +0xe8 life
    // goes below 0 (unless Slot154).
    void update(float Dt) override;
    // 101 +0x194 LAB_0053f690 (RET 0x10): unless Slot154 -- the pair (Other->Slot1a4,
    // this->Slot1ac), the CBeam's +0xf8 velocity zeroed, the beam effect flagged hit
    // (FUN_004fc8f0(1, Other == player)) and released, the companion's node dropped, then
    // Projectile's slot 108 at A.
    void Slot194(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 105 +0x1a4 LAB_0053f780: RET 0xc, empty.
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 107 +0x1ac LAB_0053f790 (RET 0x10): the munition-collision report to the server,
    // IServer::SPMunitionCollision (slot 53, +0xd4) -- in multiplayer only for the player's
    // own shots.
    void Slot1ac(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 111 +0x1bc LAB_0052e130: 0x2000000.
    unsigned Slot1bc() const override;

    void* Companion[4];       // +0xc0  FUN_005154a0; the effect list gets a copy
    FBeamEffect* BeamAlloc;   // +0xd0  FUN_0053f340's beam effect
};
static_assert(offsetof(FClientBeamGameobj, Companion) == 0xc0, "Beam +0xc0");
static_assert(offsetof(FClientBeamGameobj, BeamAlloc) == 0xd0, "Beam +0xd0");
static_assert(sizeof(FClientBeamGameobj) == 0xd4, "Beam sizeof 0xd4");

// Mine : Projectile (retail E:\FL\Scratch\Source\Client\Space\Mine.cpp has no string of its own;
// the bodies sit at 0x0053e930..0x0053eae0 beside Loot.cpp's).
class FClientMineGameobj : public FClientProjectileGameobj
{
public:
    // 0x0053ca8d..0x0053caa4: FUN_00542550(5), then Mine's three vtables.
    FClientMineGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientMineGameobj() override;

    // IObjInspect slot 1 (+0x04) LAB_0053eae0 (RET 4): a destroyed mine (Flags bit 2) reports a
    // zero velocity, any other asks IObjInspectImpl::get_velocity.
    Vector get_velocity() const override;

    // 90 +0x168 FUN_0053e930 (RET 4): FUN_00539bc0(0xf, Arch), then vt+0x4c open(Arch) on the
    // object narrowed by class & 0xf (no null test).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053e970 (RET 4): CObject::Find(Id, 0xf), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_0053e9a0 (RET 4): Projectile's update, then the CMine's vt+0x50 update --
    // UPDATE_DEFAULT when the object's owner is this connection, UPDATE_DUMB in multiplayer.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053eaa0 (RET 0xc): only a CBeam (class & 0x67) munition reaches
    // GameObject's munition-hit effect (a tail JMP to FUN_00539d80).
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 110 +0x1b8 FUN_0053ea60 (RET 4): while Field_0x34 is exactly 0, the nebula at the position
    // into DrawStateA / its fog factor into DrawStateB, and Field_0x34 = 1.0f.
    void Slot1b8(float Dt) override;
};
static_assert(sizeof(FClientMineGameobj) == 0xc0, "Mine sizeof 0xc0 (operator new(0xc0))");

// CounterMeasure : Projectile (bodies at 0x0052ff90..0x005300c0).
class FClientCounterMeasureGameobj : public FClientProjectileGameobj
{
public:
    // 0x0053cdbd..0x0053cdd4: FUN_00542550(6), then CounterMeasure's three vtables.
    FClientCounterMeasureGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientCounterMeasureGameobj() override;

    // 90 +0x168 FUN_0052ff90 (RET 4): FUN_00539bc0(0x17, Arch), open on class & 0x17.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0052ffd0 (RET 4): CObject::Find(Id, 0x17), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_00530000 (RET 4): as Mine's with class & 0x17.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053f780: RET 0xc, empty.
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 110 +0x1b8 FUN_0053ea60: Mine's.
    void Slot1b8(float Dt) override;
};
static_assert(sizeof(FClientCounterMeasureGameobj) == 0xc0, "CounterMeasure sizeof 0xc0");

// Guided : Munition -- the missile (retail Munition.cpp, .rdata 005df858; bodies at
// 0x0053ec30..0x0053f1d0).
class FClientGuidedGameobj : public FClientMunitionGameobj
{
public:
    // 0x0053d0cd..0x0053d0e4: FUN_00542550(4), then Guided's three vtables.
    FClientGuidedGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientGuidedGameobj() override;

    // 90 +0x168 FUN_0053ec30 (RET 4): FUN_00539bc0(0xa7, Arch), open on class & 0xa7.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053ec70 (RET 4): CObject::Find(Id, 0xa7), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_0053ecb0 (RET 4): as Mine's with class & 0xa7.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053eaa0: Mine's (CBeam munitions only).
    void Slot1a4(void const* A, void const* B, FClientGameobj* Munition) override;
    // 107 +0x1ac LAB_0053ee00 (RET 0x10): the munition-collision report to the server
    // (IServer::SPMunitionCollision, slot 53) -- in multiplayer only when Other belongs to this
    // connection.
    void Slot1ac(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 108 +0x1b0 FUN_0053edc0 (RET 4): the const_effect particle (OneShotCompanion +8) is let go
    // of this gameobj (FUN_004f7d20 with no owner, part -1 and the gameobj's frame), then
    // Projectile's slot 108.
    void Slot1b0(Vector const* Pos) override;
    // 110 +0x1b8 FUN_0053ea60: Mine's.
    void Slot1b8(float Dt) override;
    // 111 +0x1bc LAB_0053d430: 0x1000000.
    unsigned Slot1bc() const override;
    // 112 +0x1c0 LAB_0053f030 (RET 4): the const_effect / trail frame -- the munition
    // companion's HP_trail_parent hardpoint, else the mesh's bounds on the renderer.
    void Slot1c0(Transform* Out) override;

    // IObjAffect slot 18 (+0x48) LAB_0053eed0 (RET 0xc): re-target a seeking missile, and tell
    // the server (IServer::SetTarget) when the request came from this connection.
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override;
};
static_assert(sizeof(FClientGuidedGameobj) == 0xc0, "Guided sizeof 0xc0");

// Loot : Simple (retail E:\FL\Scratch\Source\Client\Space\Loot.cpp, .rdata 005df7e0).
//   ctor FUN_0053e470  0xb0  IObjRW 005df5d4, IObjAffect 005df554, CBase 005df548 -> FUN_0053e4b0
class FClientLootGameobj : public FClientSimpleGameobj
{
public:
    // FUN_0053e470: FUN_00547440(7), +0xac = 0, Loot's three vtables.
    FClientLootGameobj();
    // FUN_0053e4e0 (this = the CBase sub-object); deleting wrapper FUN_0053e4b0.
    ~FClientLootGameobj() override;

    // 90 +0x168 LAB_0053e550 (RET 4): FUN_00539bc0(0x803, Arch), open on class & 0x803.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c LAB_0053e5a0 (RET 4): CObject::Find(Id, 0x803), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 92 +0x170 LAB_0053e5e0: GameObject's slot 92, the crate's tractored_explosion into +0xac,
    // then the nebula lookup (FUN_005553b0) into DrawStateA / DrawStateB.
    void Slot170() override;
    // 93 +0x174 FUN_0053e6a0 (RET 4): GameObject's update, then the CLoot's vt+0x50 update.
    void update(float Dt) override;
    // 102 +0x198 FUN_0053e770 (RET 4): multiplayer only -- the root entry sets the hit points,
    // any other entry the contents' hit points (a unit lost when they ran out).
    void Slot198(DamageList const& Damage) override;
    // 103 +0x19c LAB_0053e8a0 (RET 4): DestroyType 2 (tractored in) explodes with the
    // tractored_explosion; anything else goes to Simple's slot 103.
    void Slot19c(int DestroyType) override;

    Archetype::Explosion* TractoredExplosionArch; // +0xac  slot 92 resolves; the dtor releases
};
static_assert(offsetof(FClientLootGameobj, TractoredExplosionArch) == 0xac, "Loot +0xac");
static_assert(sizeof(FClientLootGameobj) == 0xb0, "Loot sizeof 0xb0 (operator new(0xb0))");

// FUN_00549e30 -- before the docking camera takes the view, close the HUD's rear view
// (command 0x39 when HUD_Status +0x5ba == 1) and turret view (command 0xbf when +0x5b9 == 1).
// Callers: the ship's land (0x00543cb5) and launch (0x00543ebf / 0x00543eda) paths.
void CloseRearAndTurretViewsForDocking();

// operator new + the Solar constructor FUN_005475b0 (space_solar_world.cpp's solar build).
FClientGameobj* ClientGameobj_CreateSolar();

// FUN_00539bc0 (RET 8) -- CObject::Alloc(Class) into Self->Object, Arch->create_instance,
// DALib::Engine UpdateInstance(Object+4, 0, 0), CBase::advise(true). The caller opens the
// object afterwards. Callers: GameObject create (LAB_005396f0 inlines it), the ship / solar
// create tails, Beam FUN_0053f2f0, Debris FUN_005304b0.
void GameobjCreateOfClass(FClientGameobj* Self, CObject::Class ObjectClass,
                          Archetype::Root* Arch);

// FUN_00536100 -- take a reference on everything an explosion archetype will spawn: each
// effect's EffectArch (FUN_0040e960) and each innards debris simple (Root vt+0x0c, load).
// Eleven retail callers (the Gameobj/equipment explosion preloads). Null is a no-op.
void ExplosionArchAddRef(Archetype::Explosion const* Expl);
// FUN_00536170 -- the matching release: EffectArch FUN_0040e990 and Root::Release.
void ExplosionArchRelease(Archetype::Explosion const* Expl);

// The Gameobj watch list: retail std::list<BaseWatcher> DAT_0066d33c (head) / DAT_0066d340
// (count), built by the static initialiser LAB_0045a7e0 and torn down by LAB_0045a820.
// A Gameobj goes in when its +0xa8 flag is set (FUN_00547550, FUN_00539740); the HUD walks
// the list at 0x004d2102 and drops dead entries through FUN_0045a990(0).
//
// FUN_0045a890 (cdecl) -- append a watcher on Obj unless one already watches it.
void GameobjWatchListAdd(IObjRW* Obj);
// FUN_0045a990 (cdecl) -- erase the first entry watching Obj (a null Obj matches an entry
// whose object has gone), if any.
void GameobjWatchListRemove(IObjRW* Obj);
// Read access for the HUD walk: the list itself, in insertion order.
std::list<BaseWatcher> const& GameobjWatchList();
