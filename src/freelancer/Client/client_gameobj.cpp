// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_gameobj.h"

#include "DALib.h"
#include "cli_projectile_fx.h"   // CliEffectListClear / FCliEquipFirePacket / fire-host ports
#include "client_cliobj.h"       // Archetype::CliShipBayDoorSound (CliShip +0x58 / +0x5c)
#include "client_debris.h"       // FindDebrisArch / CreateDebrisGameobjForObject (FUN_00533d10)
#include "client_equip_fx.h"     // EquippedObjRebuildEquipFx / EquippedObjClearEquipFx
#include "client_explosion.h"    // Action_Explode (FUN_005361e0)
#include "client_fuse.h"         // FFuseBurnNode
#include "client_objdb.h"        // GetClientObjDb (DAT_00678a2c)
#include "common_csys.h"         // Transform (the launch camera's Csys)
#include "common_id.h"           // CreateID
#include "common_ini.h"          // INI_Reader (Solar ReadIni, FUN_005485c0)
#include "common_newchar.h"      // pub::StateGraph::get_state_graph
#include "common_server.h"       // IServer (DAT_0067ecd0)
#include "common_misc.h"         // SinglePlayer, GetFileSysPath (Common exports)
#include "dacom.h"               // FDUMP
#include "common_goods.h"        // FindHardpoint_PS / HardpointInfo
#include "common_physys.h"       // Collision::CalcCollisionDamage
#include "effect_type_info.h"    // EffectTypeWithinPerf (FUN_004fd570)
#include "engine/engbase.h"
#include "fade_to_black.h"       // SetFadeImmediate (FUN_00412070)
#include "engine_light.h"        // FGameLight, EngineLight setters (FUN_00544230)
#include "game_options.h"        // GetGameOption (FUN_005792b0)
#include "globals.h"             // G_RenderSurfaceWidth (DAT_00610850)
#include "menu_sound_arch.h"     // LookupClientSoundArch (FUN_0042ae40), Source3D factory
#ifdef FREELANCER_RP9
#include "nebula_volume.h"       // NebulaVolume::NoteOpaqueSphere (port-only Direct3D 9 nebula)
#endif
#include "nn_messaging.h"        // NnVoiceEnqueue (FUN_00489e80)
#include "nn_modal.h"            // G_bNnScreenUp (DAT_0067dcc8), NnArrivalTextShow (FUN_0045b310)
#include "hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "hud_damage_indicator.h" // GetHudDamageIndicator (DAT_00674a10), FUN_004d6d40 / 004d6cd0
#include "hud_status_core.h"     // GetHudStatus (DAT_00674aa0)
#include "jump_effect.h"         // FJumpShipEffect (Ship +0x188, dtor FUN_00503a80)
#include "gate_tunnel.h"         // FGateTunnel::Light (+0x2a8, Ship slot 97 LAB_00546480)
#include "part_cache.h"          // CopyTransform12 (FUN_00406880)
#include "render/IFLAppearance.h" // FUN_005462d0 / 00546380 / 00546400
#include "hud_waypoint.h"        // GetHudWaypoint (DAT_00674bc8), FUN_004f4d20 / 004f4ca0
#include "input_manager.h"       // GetClientMouseCursorDrawFlag (FUN_0041dd90)
#include "nn_ui_pulse.h"         // G_NnPaletteArgb_679b80 (DAT_00679b80)
#include "win3d_engine_submit.h" // FUN_00433f40 / FUN_00434630 (Solar waypoint marker)
#include "client_land_action.h"  // Action_Land (FUN_00542b70)
#include "particle_effect.h"     // FParticleEffect, ParticleEffectArmDeath / Release
#include "player_behavior.h"     // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"        // GetPlayerCharacterId (DAT_00673378), GetPlayerConnectionId
#include "render/rendcomp.h"     // IRenderer2 (Guided slot 112's extents / centre)
#include "server_connect.h"      // GetLocalServerInterface (DAT_0067ecd0)
#include "soundmanager.h"        // ISoundSource
#include "space_camera.h"        // SetDockingCameraTransform / RequestSpaceCameraMode
#include "space_scene_obj_draw.h" // GameobjPickLodRetail (FUN_00539820)
#include "space_state.h"         // SetPendingLandBase (FUN_00548ef0)
#include "star_sphere.h"         // FStarSphere (Solar +0xe4)
#include "star_sys.h"            // StarSysSuppressPlayerCollisions, GetAttitudeClass
#include "texture_panels.h"      // TexturePanelsCreate / Load (Solar "ring", FUN_005485c0)
#include "tradelane_fx.h"        // TradeLaneShipGoHandler (the LAB_00546e90 tail)
#include "vis_effect_arch.h"     // FindEffectArch / EffectArchAddRef / EffectArchRelease

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <windows.h>

namespace
{
    char const kGameobjSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Gameobj.cpp";
    // .rdata:005de4c4 -- FUN_00533d10's FDUMP file argument.
    char const kEquippedObjSourceFile[] =
        "E:\\FL\\Scratch\\Source\\Client\\Space\\EquippedObj.cpp";
    // .rdata:005dff40 -- FUN_00543b40's FDUMP file argument.
    char const kShipSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\ship.cpp";

    // The 0x18-byte node of the GameObject effect list (next, prev, 0x10 companion) -- the
    // head and the list's own teardown live here; cli_projectile_fx.cpp owns the operations.
    struct FGameobjEffectListHead
    {
        FGameobjEffectListHead* Next;
        FGameobjEffectListHead* Prev;
        void* Companion[4];
    };
    static_assert(sizeof(FGameobjEffectListHead) == 0x18, "effect list node 0x18");
}

// ===========================================================================
// GameObject effect list (+0x28)
// ===========================================================================

FGameobjEffectListStorage::FGameobjEffectListStorage(unsigned char InAllocatorByte)
{
    // Inlined in FUN_005392d0 at 0x00539340..0x00539353: the allocator byte, the head node
    // operator_new(0x18) with next = prev = itself, count 0.
    AllocatorByte = InAllocatorByte;                                           // 0x00539340
    FGameobjEffectListHead* const NewHead =
        static_cast<FGameobjEffectListHead*>(::operator new(sizeof(FGameobjEffectListHead)));
    NewHead->Next = NewHead;                                                   // 0x00539348
    NewHead->Prev = NewHead;                                                   // 0x0053934a
    Head = NewHead;                                                            // 0x00539350
    Count = 0;                                                                 // 0x00539353
}

void* FGameobjEffectListStorage::Erase(void* Where)
{
    // FUN_0053aab0 (0x0053aab0..0x0053ab18). Next is read first (0x0053aaba), then the node is
    // unlinked (0x0053aabf..0x0053aac8); the companion teardown is FUN_00502d90's sequence
    // inlined: particle +0x10 (FUN_004f7a90, cleared), sound +0xc (vt+8, cleared), light +0x14
    // (FUN_00537ad0, cleared), slot 0 (+8) = 0; the node freed, --count, *out = next.
    FGameobjEffectListHead* const Node = static_cast<FGameobjEffectListHead*>(Where);
    FGameobjEffectListHead* const Next = Node->Next;
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    CliEffectCompanionClear(Node->Companion);                                  // 0x0053aacb..0053aafc
    ::operator delete(Node);                                                   // 0x0053aaff
    Count = Count - 1;                                                         // 0x0053ab04..0053ab0b
    return Next;                                                               // 0x0053ab14
}

FGameobjEffectListStorage::~FGameobjEffectListStorage()
{
    // FUN_0053aa40 (0x0053aa40..0x0053aa97; also inlined in FUN_005393e0 at 0x0053940c..
    // 0x00539465 and, through FUN_0053ab20 / FUN_0053aab0, in FUN_005394b0 at 0x00539623..
    // 0x0053966b): erase every node from head->next up to the head, free the head, head and
    // count = 0.
    FGameobjEffectListHead* const ListHead = static_cast<FGameobjEffectListHead*>(Head);
    void* Node = ListHead->Next;
    while (Node != ListHead)
        Node = Erase(Node);
    ::operator delete(Head);                                                   // 0x0053aa84
    Head = nullptr;                                                            // 0x0053aa90
    Count = 0;                                                                 // 0x0053aa93
}

FGameobjEffectList::~FGameobjEffectList()
{
    // FUN_005393e0: FUN_004fd0d0(0) under EH state 0 (its unwind action is FUN_0053aa40, the
    // storage destructor), then the storage destructor (the base, below) on the normal path.
    CliEffectListClear(this, nullptr);                                         // 0x00539407
}

void GameobjCreateOfClass(FClientGameobj* Self, CObject::Class ObjectClass,
                          Archetype::Root* Arch)
{
    // FUN_00539bc0 (.text:00539bc0-00539c04, RET 8), the whole body: Alloc(Class) into
    // CBase::Object (+0x10); Arch->create_instance(Object) (.text:00539bd9, return value
    // dropped -- the engine stores the id at CObject+4 through
    // IEngineInstance::initialize_instance); DALib::Engine vt+0x6c UpdateInstance(Object+4,
    // 0, 0); CBase::advise(true) on this+0xc. No null tests. open is NOT part of it -- the
    // caller does that afterwards.
    Self->Object = CObject::Alloc(ObjectClass);
    Arch->create_instance(static_cast<IEngineInstance*>(Self->Object));
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Self->Object->EngineInstanceId), 0u,
                                  nullptr);
    Self->advise(true);
}

namespace
{
    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise. `this + 0xc` (the
    // CBase sub-object) is never null for a live gameobj, so the tests retail makes on it and on
    // `this` fold into the object test.
    CObject* NarrowObject(FClientGameobj const* Self, unsigned Mask)
    {
        CObject* const Obj = Self->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    // The create tail FUN_005477d0 (solar, 0x303) and LAB_00546550 (ship, 0x503) share:
    // FUN_00539bc0(Class, Arch); the object narrowed by Class gets vt+0x4c open(Arch) with no
    // null test; then, narrowed again, vt+0xc4 CEqObj::connect(INotify*) with this+0xac as a
    // tail JMP (0x0054783d / 0x005465bd).
    void CreateOpenConnect(FClientEquippedObj* Self, CObject::Class ObjectClass,
                           Archetype::Root* Arch)
    {
        GameobjCreateOfClass(Self, ObjectClass, Arch);
        unsigned const Mask = static_cast<unsigned>(ObjectClass);
        NarrowObject(Self, Mask)->open(Arch);
        static_cast<CEqObj*>(NarrowObject(Self, Mask))->connect(static_cast<INotify*>(Self));
    }

    // FUN_00547760 (solar, 0x303) / FUN_005465d0 (ship, 0x503): CObject::Find(Id, Class) into
    // +0x10, CBase::advise(true), then the narrowed object's vt+0xc4 connect(this+0xac) with no
    // null test, and `Object != 0`.
    bool FindAndConnect(FClientEquippedObj* Self, CObject::Class ObjectClass, unsigned const& Id)
    {
        // The exe imports ?Find@CObject@@SAPAU1@ABIW4Class@1@@Z -- the reference overload; the
        // slot's own reference argument is pushed through unchanged (0x0054776b / 0x005465db).
        Self->Object = CObject::Find(Id, ObjectClass);
        Self->advise(true);
        static_cast<CEqObj*>(NarrowObject(Self, static_cast<unsigned>(ObjectClass)))
            ->connect(static_cast<INotify*>(Self));
        return Self->Object != nullptr;
    }

    // The server interface every multiplayer path talks to: DAT_0067ecd0.
    IServer* LocalServer()
    {
        return static_cast<IServer*>(GetLocalServerInterface());
    }

    // DAT_00613e60 (.data, 3; its only reference is the read at 0x0053a016): how many
    // HitEffects slots FUN_00539f90 scans.
    constexpr int kHitEffectSlotCount = 3;
    // 0x0053a022: MOV [ESP+0x14],0x42c80000 -- the replace-by-lowest ceiling, 100.0f.
    constexpr float kHitEffectReplaceCeiling = 100.0f;
    static_assert(sizeof(FClientGameobj::HitEffects) / sizeof(FClientGameobj::HitEffects[0]) ==
                      kHitEffectSlotCount,
                  "HitEffects holds DAT_00613e60 slots");

    // The companion of the object's archetype, or null -- CObject::get_archetype called twice
    // with a null test on the object and on the archetype, as 0x00539d27..0x00539d42 and
    // 0x00539da5..0x00539dc3 do.
    Archetype::ICliObj* CompanionOfObjectArchetype(CObject* Obj)
    {
        if (Obj == nullptr || Obj->get_archetype() == nullptr)
            return nullptr;
        return Obj->get_archetype()->Companion;
    }

    // The hit-effect placement shared by FUN_00539f90 (0x0053a0f9..0x0053a1e4) and FUN_0053a230
    // (0x0053a377..0x0053a464): GetDirectionMatrix on {the object's +0x08 orientation copied,
    // zero translation} and the object-space direction; the result's columns taken in the order
    // 0, -2, 1 (the FCHS'd floats stored back over the matrix); the object-space point as the
    // translation. No null test on the object.
    void BuildHitEffectTransform(FTransform* Out, CObject* Obj, FVector const& Direction,
                                 FVector const& LocalPos)
    {
        Transform Basis;
        Basis.Rotation = Obj->Orientation;
        Basis.Translation.x = 0.0f;
        Basis.Translation.y = 0.0f;
        Basis.Translation.z = 0.0f;
        Vector Target;
        Target.x = Direction.X;
        Target.y = Direction.Y;
        Target.z = Direction.Z;
        Matrix const Facing = GetDirectionMatrix(Basis, Target);
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            Out->Rotation.D[Row][0] = Facing.E[Row][0];
            Out->Rotation.D[Row][1] = -Facing.E[Row][2];
            Out->Rotation.D[Row][2] = Facing.E[Row][1];
        }
        Out->Translation = LocalPos;
    }
}

// ===========================================================================
// GameObject
// ===========================================================================

FClientGameobj::FClientGameobj(EClientGameobjKind Kind)
    // 0x00539335..00539353: the effect list is built as a member (EH state 1 -> 2): its
    // allocator byte takes the kind's low byte, the head node is operator_new(0x18) with
    // next = prev = itself, count 0 (FGameobjEffectListStorage's constructor). From here on an
    // exception destroys it through FUN_005393e0 (Unwind@005c0aa8).
    : EffectList(static_cast<unsigned char>(static_cast<int>(Kind)))
{
    // FUN_005392d0 (RET 4). The IObjInspectImpl part (+0x08 Watchable, +0x10 Object zeroed,
    // the vtables) is the base constructors'; Actions (+0x20/+0x24 zeroed at 0x0053932f /
    // 0x00539332) is ActionDB's. ListNext / ListPrev (+0x14 / +0x18), CachedAttitude (+0x9c)
    // and bWatchListed (+0xa8) are not written: the ObjDB link, FUN_0053a960 and the derived
    // constructors set them.
    unsigned char const KindByte = static_cast<unsigned char>(static_cast<int>(Kind));
    ManagerKind = static_cast<int>(Kind);                                     // 0x00539328

    // 0x00539356..0053936f: the low nibble of the flag byte is cleared; the rest of it keeps
    // whatever the allocation held.
    Flags = static_cast<unsigned char>(Flags & 0xf0u);
    Field_0x34 = 0.0f;
    DrawStateA = 0;
    DrawStateB = 0;
    ExplosionArch = nullptr;
    // Field_0x48: FUN_004144b0 -- FFontString's own constructor, run as a member.
    Field_0x7c = 0;
    SoundAllocatorByte = KindByte;                                            // 0x0053937e
    SoundsBegin = nullptr;
    SoundsEnd = nullptr;
    SoundsCap = nullptr;
    Field_0xa4 = 0;
    AttitudeCountdown = -1;                                                   // 0x0053939c
    HitEffects[0] = nullptr;                                                  // 0x005393c2..c7
    HitEffects[1] = nullptr;
    HitEffects[2] = nullptr;
}

FClientGameobj::~FClientGameobj()
{
    // FUN_005394b0 (this = the CBase sub-object; the offsets below are the gameobj's).
    // 0x005394f7: FUN_004144f0 on the font string.
    FontStringClearContents(&Field_0x48);

    // 0x005394fc..00539537: every sound element's interface is stopped (vt+0x60) and released
    // (vt+8); the count is taken once, (end - begin) / 8, and a null begin counts as empty.
    int const SoundCount = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
    for (int Index = 0; Index < SoundCount; ++Index)
    {
        ISoundSource* const Source = SoundsBegin[Index].Source;
        if (Source == nullptr)
            continue;
        Source->Stop();
        SoundsBegin[Index].Source->Release();
    }

    // 0x00539539..0053955f: the three hit effects -- vt+4, FUN_004f7a90, cleared.
    for (int Index = 0; Index < 3; ++Index)
    {
        FParticleEffect* const Effect = HitEffects[Index];
        if (Effect == nullptr)
            continue;
        ParticleEffectArmDeath(Effect); // vt+4 (LAB_004f84d0, shared by both particle vtables)
        ParticleEffectRelease(HitEffects[Index]);
        HitEffects[Index] = nullptr;
    }

    CliEffectListClear(&EffectList, nullptr);                                  // 0x00539565
    Actions.free();                                                            // 0x0053956d
    if (ExplosionArch != nullptr)
        ExplosionArchRelease(ExplosionArch);                                   // 0x0053957b
    advise(false);                                                             // 0x00539586
    if (Object != nullptr)
        Object->Release();                                                     // 0x00539593

    // 0x00539599..005395a8: the sound vector's buffer.
    ::operator delete(SoundsBegin);
    SoundsBegin = nullptr;
    SoundsEnd = nullptr;
    SoundsCap = nullptr;

    // 0x005395ae..0053960c: the font string's own destructor -- FUN_004144f0 again, FUN_00418f70
    // on every child slot left (none: the clear above emptied the range), the child buffer
    // freed, then the text buffer freed through msvcrt free when there is one (the clear
    // already freed and nulled it). Called here to keep retail's member order against the
    // hand-written teardowns around it; ~FFontString then runs on an empty string.
    FontStringDestroy(&Field_0x48);

    // 0x0053960f..0053966b is the effect list's destructor inlined (FUN_005393e0's body:
    // FUN_004fd0d0(0), then the erase-every-node loop through FUN_0053ab20 / FUN_0053aab0 and
    // the head freed) -- here ~FGameobjEffectList, run as a member after ~FFontString (+0x48)
    // and before ~ActionDB (+0x20), the order retail's EH states 6 -> 7 -> 1 -> 0 give.
    // 0x00539673 ~ActionDB, 0x0053968e ~CBase and 0x005396ab Watchable::unwatch are the member
    // and base destructors that run after this body.
}

unsigned int const FClientGameobj::get_id() const
{
    // LAB_00524e70: MOV EAX,[ECX+0x10]; JZ -> 0; [EAX+0x4c] & 3 == 3 (CMP CL,3 / JNZ -> 0);
    // [EAX+0xb0].
    CObject* const Obj = NarrowObject(this, 0x3u);
    if (Obj == nullptr)
        return 0;
    return static_cast<CSimple const*>(Obj)->Id;
}

bool FClientGameobj::Slot154() const
{
    // LAB_00524e20.
    return ((Flags >> 2) & 1u) != 0;
}

bool FClientGameobj::Slot158() const
{
    // LAB_00524e30.
    return (Flags & 1u) != 0;
}

void FClientGameobj::Slot15c(bool bValue)
{
    // FUN_00539c10: bit 0 = the argument; then the object's collisions go off whatever the
    // argument was (the tail JMP pushes 0 over it, 0x00539c2c).
    Flags = static_cast<unsigned char>((Flags & ~1u) | (static_cast<unsigned>(bValue) & 1u));
    if (Object != nullptr)
        Object->enable_collisions_r(false);
}

bool FClientGameobj::Slot160() const
{
    // LAB_00524e40.
    return ((Flags >> 1) & 1u) != 0;
}

void FClientGameobj::Slot164(bool bValue)
{
    // LAB_00539c40.
    Flags = static_cast<unsigned char>((Flags & ~2u) | ((static_cast<unsigned>(bValue) << 1) & 2u));
}

void FClientGameobj::create(Archetype::Root* Arch)
{
    // LAB_005396f0: FUN_00539bc0's body inlined with class 1.
    GameobjCreateOfClass(this, CObject::ClassObject, Arch);
}

void FClientGameobj::Slot170()
{
    // FUN_00539740 -- no null tests on the object or its archetype.
    Archetype::Root* const Arch = Object->get_archetype();
    ID_String const* const ExplosionId = &Arch->ExplosionArch; // Arch+0x24

    if (!ExplosionId->IsEmpty())
    {
        ExplosionArch = Archetype::GetExplosion(*ExplosionId);                 // 0x0053976e
        if (ExplosionArch != nullptr)
        {
            ExplosionArchAddRef(ExplosionArch);                                // 0x00539774
        }
        else
        {
            // 0x00539780..005397b5: FDUMP 0x100002, Gameobj.cpp line 0x85, the archetype's
            // nickname and the explosion id's string (the id's string is fetched first).
            char const* const ExplName = ExplosionId->get_string();
            char const* const ArchName = Object->get_archetype()->Nickname.get_string();
            FDUMP(0x100002, "%s(%d) : *** WARNING: Archtype(%s) explosion_arch(%s) not found.",
                  kGameobjSourceFile, 0x85, ArchName, ExplName);
        }
    }

    // 0x005397b9..005397dd: vt+0x88 get_type (result not tested); any bit of 0x107ffdff and
    // bWatchListed -> FUN_0045a890(this).
    uint Type = 0;
    get_type(Type);
    if ((Type & 0x107ffdffu) != 0u && bWatchListed != 0)
        GameobjWatchListAdd(this);
}

void FClientGameobj::update(float Dt)
{
    // FUN_00539c60.
    Actions.update(Dt);
    Slot1b8(Dt);
}

float FClientGameobj::Slot178(Vector& Center)
{
    // FUN_00539810: the object's vt+0x64, no null test.
    return Object->get_physical_radius_r(Center);
}

unsigned* FClientGameobj::Slot17c(unsigned* OutLod, void* Camera, unsigned const* CachedLod)
{
    // FUN_00539820 -- the body is GameobjPickLodRetail (space_scene_obj_draw.cpp), which
    // EquippedObj's override FUN_00533710 also calls directly.
    return GameobjPickLodRetail(this, OutLod, Camera, CachedLod);
}

void FClientGameobj::Slot180()
{
    // LAB_00401e90: RET.
}

void FClientGameobj::Slot184(void* Camera)
{
    // FUN_00539b40 -- the body is GameobjUpdateDrawLightingRetail (space_scene_obj_draw.cpp,
    // W3-B), the same split as Slot17c / GameobjPickLodRetail.
    GameobjUpdateDrawLightingRetail(this, Camera);
}

unsigned* FClientGameobj::Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                  void* Matrix, void* Extra)
{
    // FUN_00539980 -- the body is GameobjSubmitDrawRetail (space_scene_obj_draw.cpp, W3-B),
    // the same split as Slot17c / Slot184.
    return GameobjSubmitDrawRetail(this, OutLod, Camera, CachedLod, Matrix, Extra);
}

unsigned FClientGameobj::Slot18c(unsigned* OutB)
{
    // LAB_00524e50.
    *OutB = DrawStateB;
    return DrawStateA;
}

void FClientGameobj::Slot190()
{
    // LAB_00524e60.
    DrawStateA = 0;
    DrawStateB = 0;
    Field_0x34 = 10.0f; // 0x41200000
}

void FClientGameobj::Slot194(void const* A, void const* B, FClientGameobj* Other, void const* D)
{
    // FUN_00539c90: Other->Slot1a0(A, D, this, B) (0x00539cae), then this->Slot1ac(A, B, Other,
    // D) (0x00539cc0). No null test on Other.
    Other->Slot1a0(A, D, this, B);
    Slot1ac(A, B, Other, D);
}

void FClientGameobj::Slot198(DamageList const& Damage)
{
    // LAB_0053e4a0: RET 4.
    (void)Damage;
}

void FClientGameobj::Slot19c(int DestroyType)
{
    // FUN_0053a820.
    if (DestroyType == 0)
    {
        Slot15c(true);                                                         // 0x0053a871
        return;
    }
    if (DestroyType == 1)
    {
        Flags = static_cast<unsigned char>(Flags | 4u);                        // 0x0053a853
        Slot1b0(nullptr);                                                      // 0x0053a861
        return;
    }
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kGameobjSourceFile, 0x2a4, "Invalid DestroyType!");
}

void FClientGameobj::Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                             void const* D)
{
    // FUN_00539cd0 (RET 0x10). B and D are not read.
    (void)B;
    (void)D;
    Vector const* const Hit = static_cast<Vector const*>(Contact);
    // 0x00539cd8..0x00539cfc: |Contact[0]|, summed x*x + y*y, then + z*z.
    float const Speed = sqrtf(Hit->x * Hit->x + Hit->y * Hit->y + Hit->z * Hit->z);
    // 0x00539d06 / 0x00539d11: Other's mass first, then this one's (vt+0x30 each);
    // 0x00539d1d: Collision::CalcCollisionDamage(Speed, this mass, Other's mass).
    float const OtherMass = Other->get_mass();
    float const MyMass = get_mass();
    float const Damage = Collision::CalcCollisionDamage(Speed, MyMass, OtherMass);
    // 0x00539d27..0x00539d50: this object's archetype companion (CObject::get_archetype, null
    // tests on both) picks the distance record for the damage -- FUN_00407450.
    FEffectArch* const Effect =
        Archetype::CliObjPickDistanceRecord(CompanionOfObjectArchetype(Object), Damage);
    // 0x00539d55..0x00539d67: FUN_00539f90(Other->get_id(), Effect, &Contact[0], &Contact[1], 0).
    SpawnSurfaceHitEffect(Other->get_id(), Effect, Hit, Hit + 1, nullptr);
}

void FClientGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // FUN_00539d80 (RET 0xc). A points at the hit point and the surface normal (+0x0c); B is
    // not read.
    (void)B;
    Vector const* const Hit = static_cast<Vector const*>(A);
    // 0x00539d88..0x00539daa: the munition's class & 7 object (no null test before +0x88) and
    // its Munition archetype.
    Archetype::Munition const* const MunArch =
        static_cast<Archetype::Munition const*>(NarrowObject(Munition, 0x7u)->Archetype);
    // 0x00539da5..0x00539dd3: this object's archetype companion picks the distance record for
    // the munition's hull_damage (+0x80) -- FUN_00407450.
    FEffectArch* const HullEffect =
        Archetype::CliObjPickDistanceRecord(CompanionOfObjectArchetype(Object), MunArch->HullDamage);
    // 0x00539dd8..0x00539e08: FUN_00539f90 keyed by the shot's owner (class & 0x27 object, no
    // null test before +0xe4).
    CProjectile const* const Shot = static_cast<CProjectile const*>(NarrowObject(Munition, 0x27u));
    SpawnSurfaceHitEffect(Shot->OwnerId, HullEffect, Hit, Hit + 1, nullptr);
    // 0x00539e0d..0x00539e2e: a random live effect of the munition's companion (the class & 7
    // object narrowed again) -- FUN_00408a70.
    FEffectArch* const LiveEffect = Archetype::CliMunitionPickLiveEffect(
        NarrowObject(Munition, 0x7u)->Archetype->Companion);
    // 0x00539e33..0x00539e56: the class & 1 object's vt+0x54 get_velocity (no null test).
    Vector const Velocity = NarrowObject(Munition, 0x1u)->get_velocity();
    // 0x00539e59..0x00539e61: FUN_0053a230(LiveEffect, &A[0], &A[1], &Velocity, 0) -- the
    // PUSH 0 at 0x00539e4f is its fifth argument.
    SpawnSplashHitEffect(LiveEffect, Hit, Hit + 1, &Velocity, nullptr);
}

void FClientGameobj::Slot1a8(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj)
{
    // LAB_0053f780: RET 0xc.
    (void)SubObj;
    (void)TargetId;
    (void)TargetSubObj;
}

void FClientGameobj::Slot1ac(void const* A, void const* B, FClientGameobj* Other, void const* D)
{
    // FUN_00539e70 (RET 0x10). A is not read; B and D are sub-object ids (low words).
    (void)A;
    // 0x00539e78..0x00539e84: Other's vt+0x20 get_id; 0 -> nothing.
    if (Other->get_id() == 0)
        return;
    // 0x00539e8a..0x00539eb9: in multiplayer only collisions of objects the local player owns
    // report -- Other's class & 3 object's +0xb4 OwnerPlayer, or FUN_0054bb00 (DAT_00673344)
    // when the narrow fails, against DAT_00673344.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = NarrowObject(Other, 0x3u);
        unsigned const Owner = (OtherObj != nullptr)
                                   ? static_cast<CSimple const*>(OtherObj)->OwnerPlayer
                                   : GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }
    // 0x00539ebf..0x00539ef6: this velocity minus Other's (vt+4 each, Other's first).
    Vector const OtherVelocity = Other->get_velocity();
    Vector const MyVelocity = get_velocity();
    Vector Relative;
    Relative.x = MyVelocity.x - OtherVelocity.x;
    Relative.y = MyVelocity.y - OtherVelocity.y;
    Relative.z = MyVelocity.z - OtherVelocity.z;
    // 0x00539efa..0x00539f30: this mass (vt+0x30), Other's mass, |Relative| summed x*x + z*z,
    // then + y*y; Collision::CalcCollisionDamage(|Relative|, Other's mass, this mass).
    float const MyMass = get_mass();
    float const OtherMass = Other->get_mass();
    float const Speed =
        sqrtf(Relative.x * Relative.x + Relative.z * Relative.z + Relative.y * Relative.y);
    float const Damage = Collision::CalcCollisionDamage(Speed, OtherMass, MyMass);
    // 0x00539f36..0x00539f67: {this id, B's low word zero-extended to a dword, Other's id, D's
    // low word (+0x0e never written; zero here), the damage}.
    SSPObjCollisionInfo Info;
    Info.TargetObjectId = get_id();
    Info.TargetSubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    Info.Pad_0x06 = 0;
    Info.ObjectId = Other->get_id();
    Info.SubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0e = 0;
    std::memcpy(&Info.Field_0x10, &Damage, sizeof(float));
    // 0x00539f58..0x00539f79: DAT_0067ecd0 slot 55 (+0xdc) SPObjCollision(Info, DAT_00673344),
    // no null test.
    LocalServer()->SPObjCollision(Info, GetPlayerConnectionId());
}

void FClientGameobj::Slot1b0(Vector const* Pos)
{
    // FUN_0053a880.
    if (ExplosionArch == nullptr)
    {
        Slot15c(true);                                                         // 0x0053a8e2
        return;
    }
    CliEffectListClear(&EffectList, nullptr);                                  // 0x0053a892
    // 0x0053a897..0053a8d5: Pos, or vt+0 get_position() when it is null, copied to the stack;
    // FUN_005361e0(ExplosionArch, this, &Copy) builds the explode action and ActionDB::add
    // (+0x20) takes it.
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Vector const Copy = *Where;
    Actions.add(Action_Explode::Create(ExplosionArch, this, &Copy));
}

void FClientGameobj::Slot1b4(unsigned SourceId, void* SoundArch, Vector const* Pos)
{
    // FUN_0053a4c0 (RET 0xc). SoundArch is a client sound archetype (SoundArch.cpp).
    // 0x0053a4c6..0x0053a4df: none, or one whose slot 12 (+0x30, the lod link) is null -> nothing.
    if (SoundArch == nullptr || GetClientSoundArchLinked(SoundArch) == nullptr)
        return;

    // 0x0053a4e5..0x0053a507: the pool's size, 0 with no block.
    int const Count = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
    ISoundSource* Voice = nullptr;

    // 0x0053a516..0x0053a55b: the first entry keyed to SourceId whose voice's archetype (+0x18)
    // has the id (+4) of this archetype's link.
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        FGameobjSound* const Entry = &SoundsBegin[Index];
        if (Entry->Field_0x00 != SourceId)
            continue;
        void* const VoiceArch = GetClientSoundSourceArch(Entry->Source);
        void* const Linked = GetClientSoundArchLinked(SoundArch);
        if (GetClientSoundArchId(VoiceArch) == GetClientSoundArchId(Linked))
        {
            Voice = Entry->Source;
            break;
        }
    }

    // 0x0053a561..0x0053a5a7: otherwise the first entry whose voice is idle (vt+0x14 IsActive
    // false) is taken over for SourceId.
    if (Voice == nullptr)
    {
        for (Index = 0; Index < Count; ++Index)
        {
            FGameobjSound* const Entry = &SoundsBegin[Index];
            if (!Entry->Source->IsActive())
            {
                Voice = Entry->Source;
                Entry->Field_0x00 = SourceId;
                break;
            }
        }
    }

    // 0x0053a5ab..0x0053a7ba: otherwise a new Source3D (FUN_00428f80(0x64) + FUN_00428790),
    // posed on this object's engine instance (vt+0x1c get_index) with an identity transform
    // (vt+0x94), and appended to the pool -- the VC6 vector insert at end, growing by
    // max(size, 1).
    if (Voice == nullptr)
    {
        Voice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
        Transform Pose;
        Pose.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Pose.Translation.x = 0.0f;
        Pose.Translation.y = 0.0f;
        Pose.Translation.z = 0.0f;
        int const Attach = static_cast<int>(get_index());
        Voice->SetPose(Attach, reinterpret_cast<float const*>(&Pose));

        FGameobjSound Added;
        Added.Field_0x00 = SourceId;
        Added.Source = Voice;
        if (SoundsCap - SoundsEnd < 1)
        {
            int const Size = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
            int const Grow = (SoundsBegin == nullptr || Size <= 1) ? 1 : Size;
            int NewCapacity = Size + Grow;
            if (NewCapacity < 0)
                NewCapacity = 0;
            FGameobjSound* const Block = static_cast<FGameobjSound*>(
                ::operator new(static_cast<size_t>(NewCapacity) * sizeof(FGameobjSound)));
            int Copy = 0;
            for (Copy = 0; Copy < Size; ++Copy)
                Block[Copy] = SoundsBegin[Copy];
            Block[Size] = Added;
            ::operator delete(SoundsBegin);
            SoundsCap = Block + NewCapacity;
            SoundsEnd = Block + Size + 1;
            SoundsBegin = Block;
        }
        else
        {
            *SoundsEnd = Added;
            ++SoundsEnd;
        }
    }

    // 0x0053a7bd..0x0053a80a: the voice goes to Pos (vt+0x8c), takes the archetype's random
    // pitch (slot 11, +0x2c) through vt+0x98, and plays: a voice already playing is rewound
    // (vt+0x64 SetStartOffset(0)), an idle one starts on the archetype (vt+0x58 Start(Arch, 0)).
    Voice->Slot35(const_cast<Vector*>(Pos));
    Voice->SetPitch(GetClientSoundArchRandomPitch(SoundArch));
    if (Voice->IsActive())
        Voice->SetStartOffset(0);
    else
        Voice->Start(SoundArch, 0);
}

void FClientGameobj::SpawnSurfaceHitEffect(unsigned SourceId, FEffectArch* EffectArch,
                                           Vector const* Pos, Vector const* Normal,
                                           void* CompanionOut)
{
    // FUN_00539f90 (RET 0x14). CompanionOut ([ESP+0xb0] after the four pushes) is never read.
    (void)CompanionOut;
    // 0x00539f9a..0x00539faf: the player's ship shows none of this while SURFACE_HIT_EFFECTS
    // (FUN_005792b0(0x38)) is off.
    if (static_cast<void*>(this) == GetPlayerObjRW() &&
        GetGameOption(kGameOptSurfaceHitEffects) == 0)
        return;
    // 0x00539fb5..0x00539fbe.
    if (EffectArch == nullptr)
        return;

    // 0x00539fc4..0x00539fca: no VisEffect (+0x08) -> straight to the sound.
    if (EffectArch->VisEffect != nullptr)
    {
        // 0x00539fd0..0x0053a003: FUN_004f79a0(0x84) + FUN_004f8060(0), then its vt[0] start
        // with the arch -- no null test on the allocation.
        FParticleEffect* const Effect = ParticleEffectCreate();
        if (ParticleEffectStart(Effect, EffectArch) == 0)
        {
            ParticleEffectRelease(Effect);                                     // 0x0053a00b
        }
        else
        {
            // 0x0053a015..0x0053a05f: the slot -- the first empty or dying (Flags08 bit 1) one,
            // else the one whose +0x78 is lowest below 100.0f (TEST AH,5 / JP: strictly below,
            // ordered), else slot 0.
            int Chosen = 0;
            float Best = kHitEffectReplaceCeiling;
            int Index = 0;
            for (Index = 0; Index < kHitEffectSlotCount; ++Index)
            {
                FParticleEffect* const Slot = HitEffects[Index];
                if (Slot == nullptr || (Slot->Flags08 & 2u) != 0)
                {
                    Chosen = Index;
                    break;
                }
                if (Slot->RunTime78 < Best)
                {
                    Best = Slot->RunTime78;
                    Chosen = Index;
                }
            }
            // 0x0053a061..0x0053a089: the old one cleaned up (FUN_004f8110), its vt+4 stop,
            // released (FUN_004f7a90) and cleared. Every HitEffects entry is a base
            // ParticleEffect made right here, so its vt+4 is LAB_004f84d0.
            if (HitEffects[Chosen] != nullptr)
            {
                ParticleEffectCleanupAlchemy(HitEffects[Chosen]);
                ParticleEffectArmDeath(HitEffects[Chosen]);
                ParticleEffectRelease(HitEffects[Chosen]);
                HitEffects[Chosen] = nullptr;
            }
            HitEffects[Chosen] = Effect;                                       // 0x0053a098

            // 0x0053a09f..0x0053a0f6: Pos and Normal into object space through DAT_006789d0
            // (vt+0x3c InverseTransform, vt+0x34 InverseRotate on the object's +0x08
            // transform, no null test on the object).
            FTransform const& ObjXform =
                reinterpret_cast<FTransform const&>(Object->get_transform());
            FVector LocalPos;
            GameobjMathEngine()->InverseTransform(LocalPos, ObjXform,
                                                  reinterpret_cast<FVector const&>(*Pos));
            FVector LocalNormal;
            GameobjMathEngine()->InverseRotate(LocalNormal, ObjXform,
                                               reinterpret_cast<FVector const&>(*Normal));
            // 0x0053a0f9..0x0053a1f5: the orientation along the local normal, placed at the
            // local point, attached to this gameobj (FUN_004f7d20).
            FTransform AttachXform;
            BuildHitEffectTransform(&AttachXform, Object, LocalNormal, LocalPos);
            ParticleEffectAttach(Effect, this, static_cast<int>(get_index()), &AttachXform);
        }
    }

    // 0x0053a202..0x0053a21f: the sound arch (+0x18) -> Slot1b4(SourceId, Sound, Pos).
    if (EffectArch->SndEffect != nullptr)
        Slot1b4(SourceId, EffectArch->SndEffect, Pos);
}

void FClientGameobj::SpawnSplashHitEffect(FEffectArch* EffectArch, Vector const* Pos,
                                          Vector const* Normal, Vector const* Velocity,
                                          void* CompanionOut)
{
    // FUN_0053a230 (RET 0x14).
    // 0x0053a23a..0x0053a24f: the player's ship shows none of this while SPLASH_HIT_EFFECTS
    // (FUN_005792b0(0x39)) is off.
    if (static_cast<void*>(this) == GetPlayerObjRW() &&
        GetGameOption(kGameOptSplashHitEffects) == 0)
        return;
    // 0x0053a255..0x0053a25e.
    if (EffectArch == nullptr)
        return;
    // 0x0053a264..0x0053a272: FUN_004fd570(EffectType).
    if (!EffectTypeWithinPerf(EffectArch->EffectType))
        return;

    // 0x0053a278..0x0053a2fe: Pos (InverseTransform), Normal and Velocity (InverseRotate) into
    // object space. The object-space velocity is written and never read again: the buffer is
    // overwritten by the normal's copy at 0x0053a343..0x0053a355.
    FTransform const& ObjXform = reinterpret_cast<FTransform const&>(Object->get_transform());
    FVector LocalPos;
    GameobjMathEngine()->InverseTransform(LocalPos, ObjXform,
                                          reinterpret_cast<FVector const&>(*Pos));
    FVector LocalNormal;
    GameobjMathEngine()->InverseRotate(LocalNormal, ObjXform,
                                       reinterpret_cast<FVector const&>(*Normal));
    FVector LocalVelocity;
    GameobjMathEngine()->InverseRotate(LocalVelocity, ObjXform,
                                       reinterpret_cast<FVector const&>(*Velocity));

    // 0x0053a301..0x0053a321: FUN_00422b80 on the negated WORLD velocity.
    Vector Incoming;
    Incoming.x = -Velocity->x;
    Incoming.y = -Velocity->y;
    Incoming.z = -Velocity->z;
    // 0x0053a326..0x0053a372: the object-space normal scaled by 2 * (N.z*I.z + N.y*I.y +
    // N.x*I.x) (FUN_00507ea0), minus Incoming (FUN_0053a490).
    float const Scale = (LocalNormal.Z * Incoming.z + LocalNormal.Y * Incoming.y +
                         LocalNormal.X * Incoming.x) * 2.0f;
    FVector Direction;
    Direction.X = LocalNormal.X * Scale - Incoming.x;
    Direction.Y = LocalNormal.Y * Scale - Incoming.y;
    Direction.Z = LocalNormal.Z * Scale - Incoming.z;

    // 0x0053a377..0x0053a46b: the orientation along that direction, at the local point.
    FTransform EffectXform;
    BuildHitEffectTransform(&EffectXform, Object, Direction, LocalPos);
    // 0x0053a46e..0x0053a47b: FUN_00503070 on DAT_00674c60 (EffectArch, this, get_index,
    // &EffectXform, CompanionOut).
    CliCreateEffectOneShot(EffectArch, this, static_cast<int>(get_index()), &EffectXform,
                           CompanionOut);
}

void FClientGameobj::Slot1b8(float Dt)
{
    // LAB_0053e4a0: RET 4.
    (void)Dt;
}

int FClientGameobj::GetCachedAttitude()
{
    // FUN_0053a960.
    int const Countdown = AttitudeCountdown;
    if (Countdown > 0)
    {
        AttitudeCountdown = Countdown - 1;                                     // 0x0053a9ae
        return CachedAttitude;
    }
    // 0x0053a96d..0053a97b: SETGE / DEC / AND 0xfffffff9 / ADD 8 -- 8 when the countdown was
    // exactly 0, 1 when it was negative (the ctor's -1: the first call refreshes again soon).
    AttitudeCountdown = (Countdown >= 0) ? 8 : 1;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());           // FUN_0054baf0
    if (Player == nullptr)
    {
        CachedAttitude = 0;                                                    // 0x0053a99c
        return CachedAttitude;
    }
    // 0x0053a98a..0053a98c: PUSH this, PUSH player -- FUN_0045a490(player, this).
    CachedAttitude = GetAttitudeClass(Player, this);
    return CachedAttitude;
}

bool FClientGameobj::GetFlag3IfSurfaceWidthUnchanged() const
{
    // FUN_0053a9f0.
    if (static_cast<unsigned>(Field_0x7c) != G_RenderSurfaceWidth)
        return false;
    return ((Flags >> 3) & 1u) != 0;
}

void FClientGameobj::SetFlag3(bool bValue)
{
    // FUN_0053aa10.
    Flags = static_cast<unsigned char>((Flags & ~8u) | ((static_cast<unsigned>(bValue) << 3) & 8u));
    if ((Flags & 8u) != 0)
        Field_0x7c = static_cast<int>(G_RenderSurfaceWidth);                   // 0x0053aa2c
}

// IObjAffect -- FClientGameobj's own table, .rdata:005de76c (FUN_005392d0 stores it at +4,
// .text:005393ac). PORTED 2026-09-24: each entry is a retail one-instruction error stub,
//   .text:0053a950 MOV EAX,-3 / RET 4      .text:0053a900 MOV EAX,-8 / RET 4
//   .text:004b5010 XOR EAX,EAX / RET       .text:0053a920 MOV EAX,-3 / RET 0x10
//   .text:0053a910 MOV EAX,-3 / RET 0xc    .text:0053a940 MOV EAX,-3 / RET 8
//   .text:0053a8f0 MOV EAX,-3 / RET 0x14   .text:0053a930 MOV EAX,-3 / RET
// -- the same pattern as Server.dll GameObject's .rdata:06d6576c. Simple (005e0024) and Solar
// (005e0284) keep every entry; EquippedObj and Ship override some (see below).
int FClientGameobj::set_throttle(float) { return -3; }                                   // 0053a950
int FClientGameobj::set_axis_throttle(Vector const&) { return -3; }                      // 0053a950
int FClientGameobj::set_nudge_vec(Vector const&) { return -3; }                          // 0053a950
int FClientGameobj::set_strafe_dir(StrafeDir) { return -3; }                             // 0053a950
int FClientGameobj::set_formation_leader(IObjRW*) { return -8; }                         // 0053a900
int FClientGameobj::set_formation_offset(Vector const&) { return -8; }                   // 0053a900
int FClientGameobj::add_formation_follower(IObjRW*) { return -8; }                       // 0053a900
int FClientGameobj::remove_formation_follower(IObjRW*) { return -8; }                    // 0053a900
int FClientGameobj::get_formation_follower_count() { return 0; }                         // 004b5010
int FClientGameobj::set_follow_leader(IObjRW*) { return -8; }                            // 0053a900
int FClientGameobj::set_follow_offset(Vector const&) { return -8; }                      // 0053a900
int FClientGameobj::add_follow_follower(IObjRW*) { return -8; }                          // 0053a900
int FClientGameobj::remove_follow_follower(IObjRW*) { return -8; }                       // 0053a900
int FClientGameobj::slot_34(unsigned short const*, unsigned, Vector const*, int) { return -3; }  // 0053a920
int FClientGameobj::slot_38(unsigned short, int, int) { return -3; }                     // 0053a910
int FClientGameobj::slot_3c(unsigned short const*, std::vector<IObjRW*> const&, int) { return -3; }  // 0053a910
int FClientGameobj::slot_40(unsigned short const*, IObjRW*, int) { return -3; }          // 0053a910
int FClientGameobj::slot_44(unsigned short const*, IObjRW*, unsigned short, int) { return -3; }  // 0053a920
int FClientGameobj::set_target(IObjRW*, unsigned short, int) { return -3; }              // 0053a910
int FClientGameobj::slot_4c(unsigned short const*, bool, int) { return -3; }             // 0053a910
int FClientGameobj::slot_50(bool, bool, int) { return -3; }                              // 0053a910
int FClientGameobj::slot_54(bool, int) { return -3; }                                    // 0053a940
int FClientGameobj::slot_58(unsigned short, unsigned, int) { return -3; }                // 0053a910
int FClientGameobj::slot_5c(int, unsigned const*, unsigned const*, int, RequestCB*) { return -3; }  // 0053a8f0
int FClientGameobj::slot_60(int, unsigned const*, int) { return -3; }                    // 0053a910
int FClientGameobj::go_tradelane(IObjInspect const*, IObjInspect const*, bool, float) { return -3; }  // 0053a920
int FClientGameobj::stop_tradelane() { return -3; }                                      // 0053a930
int FClientGameobj::slot_6c(int, int) { return -3; }                                     // 0053a940
int FClientGameobj::slot_70(unsigned) { return -3; }                                     // 0053a950
int FClientGameobj::slot_74(unsigned) { return -3; }                                     // 0053a950

// ===========================================================================
// Simple
// ===========================================================================

FClientSimpleGameobj::FClientSimpleGameobj(EClientGameobjKind Kind)
    : FClientGameobj(Kind)
{
    // FUN_00547440: GameObject's ctor, then +0xa8 = 0 (0x0054744d).
    bWatchListed = 0;
}

FClientSimpleGameobj::~FClientSimpleGameobj()
{
    // FUN_00547470 -> thunk_FUN_005394b0: nothing of Simple's own.
}

void FClientSimpleGameobj::Slot198(DamageList const& Damage)
{
    // FUN_005474a0: multiplayer only. Every entry of the list whose sub-object is the root
    // (SUBOBJ_ID_ROOT) sets the CSimple's hit points through vt+0x98; the object is narrowed
    // with no null test on the result.
    if (SinglePlayer())
        return;
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        if (It->SubObjId != SUBOBJ_ID_ROOT)
            continue;
        static_cast<CSimple*>(NarrowObject(this, 3u))->set_hit_pts(It->HitPts);
    }
}

void FClientSimpleGameobj::Slot19c(int DestroyType)
{
    // FUN_00547500: CSimple::set_hit_pts(0) (vt+0x98, no null test), then FUN_0053a820.
    static_cast<CSimple*>(NarrowObject(this, 3u))->set_hit_pts(0.0f);
    FClientGameobj::Slot19c(DestroyType);
}

unsigned FClientSimpleGameobj::Slot1bc() const
{
    // LAB_00547530: the CSimple's +0xe0, no null test.
    return static_cast<CSimple const*>(NarrowObject(this, 3u))->TypeBits;
}

void FClientSimpleGameobj::SetWatchListed(int Unused, int bWatch)
{
    // FUN_00547550 (RET 8): the first argument is never read.
    (void)Unused;
    bool const bOn = (bWatch == 1);
    bWatchListed = bOn ? 1 : 0;                                                // 0x0054755a
    if (bOn)
        GameobjWatchListAdd(this);                                             // 0x0054756e
    else
        GameobjWatchListRemove(this);                                          // 0x00547563
}

// ===========================================================================
// EquippedObj
// ===========================================================================

namespace
{
    // The 0xc-byte node of the equip-FX list.
    struct FEquipFxNode
    {
        FEquipFxNode* Next;
        FEquipFxNode* Prev;
        INotify* Wrapper;
    };
    static_assert(sizeof(FEquipFxNode) == 0xc, "equip-FX node 0xc");
}

FEquippedFuseList::FEquippedFuseList(unsigned char InAllocatorByte)
{
    // Inlined in FUN_00532bc0: the allocator byte (0x00532c23), the head operator_new(0x40)
    // with only next = prev = itself written (0x00532c29..0x00532c30; the ClientFuse payload
    // stays raw memory), count 0 (0x00532c39).
    AllocatorByte = InAllocatorByte;
    FFuseBurnNode* const NewHead = static_cast<FFuseBurnNode*>(::operator new(sizeof(FFuseBurnNode)));
    NewHead->Next = NewHead;
    NewHead->Prev = NewHead;
    Head = NewHead;
    Count = 0;
}

FEquippedFuseList::~FEquippedFuseList()
{
    // FUN_005359c0 (0x005359c0..0x00535a18): from head->next up to the head -- next read first,
    // unlink, the fuse's vt+0 with 0 (its destructor, no delete), the node freed, --count; then
    // the head freed and head = count = 0. `delete Node` runs ~FFuseBurnNode (the ClientFuse
    // destructor) and frees the node, the same two steps.
    FFuseBurnNode* const ListHead = static_cast<FFuseBurnNode*>(Head);
    FFuseBurnNode* Node = ListHead->Next;
    while (Node != ListHead)
    {
        FFuseBurnNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        Count = Count - 1;
        Node = Next;
    }
    ::operator delete(Head);                                                   // 0x00535a05
    Head = nullptr;                                                            // 0x00535a11
    Count = 0;                                                                 // 0x00535a14
}

FEquipFxList::FEquipFxList(unsigned char InAllocatorByte)
{
    // Inlined in FUN_00532bc0: the allocator byte (0x00532bfc), the head operator_new(0xc) with
    // next = prev = itself (0x00532c02..0x00532c09), count 0 (0x00532c12).
    AllocatorByte = InAllocatorByte;
    FEquipFxNode* const NewHead = static_cast<FEquipFxNode*>(::operator new(sizeof(FEquipFxNode)));
    NewHead->Next = NewHead;
    NewHead->Prev = NewHead;
    Head = NewHead;
    Count = 0;
}

FEquipFxList::~FEquipFxList()
{
    // FUN_004a2d30 (0x004a2d30..0x004a2d7e, the shared pointer-list destructor): from
    // head->next up to the head -- next read first, unlink, the node freed (the wrapper it
    // points at is not touched), --count; then the head freed and head = count = 0.
    FEquipFxNode* const ListHead = static_cast<FEquipFxNode*>(Head);
    FEquipFxNode* Node = ListHead->Next;
    while (Node != ListHead)
    {
        FEquipFxNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);                                               // 0x004a2d54
        Count = Count - 1;
        Node = Next;
    }
    ::operator delete(Head);                                                   // 0x004a2d6b
    Head = nullptr;
    Count = 0;
}

FClientEquippedObj::FClientEquippedObj(EClientGameobjKind Kind)
    : FClientSimpleGameobj(Kind)
    // 0x00532bf0..0x00532c12: the equip-FX list built as a member (EH state 0 -> 1; unwind
    // FUN_004a2d30, Unwind@005c07cb).
    , EquipFx(static_cast<unsigned char>(static_cast<int>(Kind)))
    // 0x00532c18..0x00532c39: the fuse list built as a member (EH state 1 -> 2; unwind
    // FUN_005359c0, Unwind@005c07d9).
    , Fuses(static_cast<unsigned char>(static_cast<int>(Kind)))
{
    // FUN_00532bc0 (RET 4). The flag store at 0x00532be9 sits before the two list builds in
    // retail; a byte of this object nobody reads while they run.
    EquipFlags = static_cast<unsigned char>(EquipFlags & 0xfeu);               // 0x00532be9

    // 0x00532c4e..0x00532cb5: the sub-target map at +0xcc built empty (comparator/allocator
    // bytes, nil node, head node, size 0) -- SubTargets' own constructor in the port.
    Field_0xe0 = 0;                                                            // 0x00532cbe
}

FClientEquippedObj::~FClientEquippedObj()
{
    // FUN_00532d40 (this = the CBase sub-object; the offsets below are the gameobj's).
    // 0x00532d83..00532de0: every equip-FX wrapper deleted through its vt+4(1), its node erased
    // (FUN_0053ab20) -- FUN_00533080's body. The port's wrapper deletion lives in
    // client_equip_fx.cpp.
    EquippedObjClearEquipFx(this);

    // 0x00532de9..0x00532e0e: the object narrowed by class & 0x103 (null when it fails; the
    // walk below reads through it with no further test).
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
    // 0x00532e14..0x00532e5f: every fuse of the archetype's fuse list (arch +0x78 list head;
    // the port's Archetype::EqObj keeps a vector* in that list's slot, null for an empty list)
    // looked up in the fuse DB (FUN_0052a680 + FUN_00432240) and, when found, Fuse::UnLoad'ed.
    Archetype::EqObj const* const EqArch = static_cast<Archetype::EqObj const*>(EqObj->Archetype);
    if (EqArch->Fuses != nullptr)
    {
        std::vector<Archetype::FGroupFuse> const& ArchFuses = *EqArch->Fuses;
        size_t FuseIndex = 0;
        for (FuseIndex = 0; FuseIndex < ArchFuses.size(); ++FuseIndex)
        {
            ClientFuse* const Proto = ClientFuseDB::Find(ArchFuses[FuseIndex].Fuse.Id);
            if (Proto != nullptr)
                Proto->UnLoad();                                               // 0x00532e53
        }
    }
    // 0x00532e61..0x00532edd: the same for every archgroup's fuse ignitions --
    // CArchGroupManager::Traverse at CEqObj+0x144 with a fresh traverser, CArchGroup::
    // FuseIgnitions(), each entry found in the fuse DB and UnLoad'ed.
    CArchGrpTraverser GroupCursor;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupCursor);
    while (Group != nullptr)
    {
        Archetype::FuseIgnitionList const& Ignitions = Group->FuseIgnitions();
        if (Ignitions.Items != nullptr)
        {
            std::vector<Archetype::FGroupFuse> const& GroupFuses = *Ignitions.Items;
            size_t GroupFuseIndex = 0;
            for (GroupFuseIndex = 0; GroupFuseIndex < GroupFuses.size(); ++GroupFuseIndex)
            {
                ClientFuse* const Proto = ClientFuseDB::Find(GroupFuses[GroupFuseIndex].Fuse.Id);
                if (Proto != nullptr)
                    Proto->UnLoad();                                           // 0x00532ebe
            }
        }
        Group = EqObj->ArchGroups.Traverse(GroupCursor);
    }

    // 0x00532edf..00532f24: the sub-target map at +0xcc emptied (FUN_00535ad0, erase over
    // [begin, end)), then its head and nil nodes freed -- here, in retail's order; SubTargets'
    // own destructor frees the rest after the body.
    SubTargets.clear();

    // 0x00532f27..00532f57 is the fuse list's destructor inlined (FUN_00535a80 over
    // [begin, end), head freed) and 0x00532f5a..00532f8e the equip-FX list's (FUN_004a2f80 over
    // [begin, end) -- empty after the clear above -- head freed): ~FEquippedFuseList (+0xc0) and
    // ~FEquipFxList (+0xb4), run as members after ~SubTargets (+0xcc), in that order.
    // 0x00532f99: FUN_005394b0 -- GameObject's destructor runs next.
}

void FClientEquippedObj::notify(Event Code, void* Param)
{
    // FUN_00534730 (INotify slot 0 on the +0xac sub-object, RET 8): switch on Code - 5
    // (JA above 3 -> nothing). The numeric codes are Common's; none of 5..8 has a name in
    // INotify::Event yet.
    switch (static_cast<int>(Code))
    {
    case 5:
    {
        // 0x0053474c..0x005347af: an attached part came off. Its debris object (taken over:
        // RetrieveDebrisObject, before the slot-85 test) goes to FUN_00533d10 with
        // &arch->DebrisType (+0x74) and, unless slot 85 (+0x154) is set,
        // &arch->SeparationExplosion (+0x70). AttachedEquipArch is fetched for +0x70 first.
        CAttachedEquip* const Attached = static_cast<CAttachedEquip*>(Param);
        CObject* const PartObject = Attached->RetrieveDebrisObject();
        if (Slot154())
        {
            SpawnPartDebris(PartObject, &Attached->AttachedEquipArch()->DebrisType, nullptr);
        }
        else
        {
            ID_String const* const Explosion = &Attached->AttachedEquipArch()->SeparationExplosion;
            SpawnPartDebris(PartObject, &Attached->AttachedEquipArch()->DebrisType, Explosion);
        }
        break;
    }
    case 6:
        // 0x005347b2..0x005347c7: the part's effects stop -- FUN_004fd230(GetRootIndex (vt+0x84),
        // no new owner) on the effect list (+0x28). The 0 pushed before the virtual call is
        // FUN_004fd230's second argument.
        CliEffectListDetachPart(&EffectList,
                                static_cast<CAttachedEquip*>(Param)->GetRootIndex(), nullptr);
        break;
    case 7:
    {
        // 0x005347d2..0x0053482c: a CArchGroup was destroyed -- FUN_00533d10(the destroyed
        // object (RetreiveDestroyedObj, before the slot-85 test), &DebrisTypeName(),
        // slot 85 ? null : &ExplosionTypeName()); ExplosionTypeName is fetched first.
        CArchGroup* const Group = static_cast<CArchGroup*>(Param);
        CObject* const PartObject = Group->RetreiveDestroyedObj();
        if (Slot154())
        {
            SpawnPartDebris(PartObject, &Group->DebrisTypeName(), nullptr);
        }
        else
        {
            ID_String const* const Explosion = &Group->ExplosionTypeName();
            SpawnPartDebris(PartObject, &Group->DebrisTypeName(), Explosion);
        }
        break;
    }
    case 8:
        // 0x0053482f..0x00534842: the group's effects stop -- FUN_004fd230(GetRootIndex, 0).
        CliEffectListDetachPart(&EffectList, static_cast<CArchGroup*>(Param)->GetRootIndex(),
                                nullptr);
        break;
    default:
        break;
    }
}

void FClientEquippedObj::SpawnPartDebris(CObject* PartObject, ID_String const* DebrisType,
                                         ID_String const* Explosion)
{
    // FUN_00533d10 (RET 0xc).
    // 0x00533d18..0x00533d70: the [Debris] record; a miss warns (the part archetype's nickname
    // string first, then the type's, then get_id, vt+0x20) and takes the default.
    FDebrisArch* DebrisArch = FindDebrisArch(DebrisType);
    if (DebrisArch == nullptr)
    {
        char const* const ArchName = PartObject->get_archetype()->Nickname.get_string();
        char const* const TypeName = DebrisType->get_string();
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: ID(0x%X) Exploding: Couldn't find debris_type[%s] with "
              "archetype[%s], using default.",
              kEquippedObjSourceFile, 0x1e8, get_id(), TypeName, ArchName);
        DebrisArch = GetDefaultDebrisArch();
    }

    // 0x00533d75..0x00533d89: the Debris gameobj (FUN_00530620), and the part's effects move
    // to it (FUN_004fd230 on +0x28 with the part's engine instance, +0x04).
    FClientDebrisGameobj* const Debris = CreateDebrisGameobjForObject(PartObject, DebrisArch);
    CliEffectListDetachPart(&EffectList, PartObject->EngineInstanceId, Debris);

    // 0x00533d8e..0x00533dfa: a named explosion starts on the Debris gameobj at this Gameobj's
    // position (vt+0, the reference passed as is); an unknown one warns.
    if (Explosion == nullptr || Explosion->IsEmpty())
        return;
    Archetype::Explosion* const Expl = Archetype::GetExplosion(*Explosion);
    if (Expl != nullptr)
    {
        Debris->Actions.add(Action_Explode::Create(Expl, Debris, &get_position()));
        return;
    }
    FDUMP(0x100002, "%s(%d) : *** WARNING: Couldn't find separation_explosion[%s], skipping.",
          kEquippedObjSourceFile, 0x202, Explosion->get_string());
}

void FClientEquippedObj::Slot170()
{
    // FUN_00533e10 -- ported as EquippedObjRebuildEquipFx (client_equip_fx.cpp).
    EquippedObjRebuildEquipFx(this);
}

void FClientEquippedObj::update(float Dt)
{
    // FUN_00534010 (RET 4). Callers: slot 93 of every EquippedObj class that does not override
    // it, the ship update FUN_00545050 (0x00545157) and the solar update (0x005478c6), both
    // called directly.
    // 0x00534018..0x0053401b: GameObject's update FUN_00539c60 first.
    FClientGameobj::update(Dt);

    // 0x00534020..0x0053407e: the equip-FX list at +0xb8 (FEquipFxListNode, client_equip_fx.h),
    // the head re-read at every loop test (0x00534076). Each wrapper's slot 2 (+8, Update) gets
    // Dt; one that answers false is deleted through slot 1 (+4) with 1 -- after a null test
    // (0x00534044) -- its node unlinked and freed (FUN_005b7e1d, operator delete), and +0xbc
    // decremented; the walk goes on from the old Next.
    FEquipFxListNode* Node = static_cast<FEquipFxListNode*>(EquipFx.Head)->Next;
    while (Node != static_cast<FEquipFxListNode*>(EquipFx.Head))
    {
        if (Node->Wrapper->Update(Dt))
        {
            Node = Node->Next;
            continue;
        }
        delete Node->Wrapper;
        FEquipFxListNode* const Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        ::operator delete(Node);
        EquipFx.Count = EquipFx.Count - 1;
        Node = Next;
    }

    // 0x0053407e..0x0053409a: every burning fuse at +0xc4, Fuse::Burn(Dt) on its ClientFuse
    // (node +8), head to head with no other test.
    FFuseBurnNode* const FuseListHead = static_cast<FFuseBurnNode*>(Fuses.Head);
    for (FFuseBurnNode* Node = FuseListHead->Next; Node != FuseListHead; Node = Node->Next)
        Node->Live.Burn(Dt);
}

unsigned* FClientEquippedObj::Slot17c(unsigned* OutLod, void* Camera, unsigned const* CachedLod)
{
    // FUN_00533710 (RET 0xc). 0x00533710..0x00533735: the object narrowed to a CEqObj (0x103,
    // else null -- the test of `this` is only the CBase this-adjustment), its +0x140 read with
    // no further test. 0x0053373b..0x00533757: >= 1.0f (FCOMP, TEST AH,1 / JNZ skips below and
    // unordered) -> LOD {1, 0xffff}, i.e. not drawn. Otherwise GameObject's pick FUN_00539820,
    // called directly (0x0053376a).
    CEqObj const* const EqObj = static_cast<CEqObj const*>(NarrowObject(this, 0x103u));
    if (EqObj->CloakPercent >= 1.0f)
    {
        *OutLod = 0xffff0001u;
        return OutLod;
    }
    return GameobjPickLodRetail(this, OutLod, Camera, CachedLod);
}

void FClientEquippedObj::Slot180()
{
    // LAB_0051b480: E9 0B6AEEFF = JMP 0x00401e90 (Ghidra labels the ICF-folded target
    // IDPMsgHandler::OnDisconnect), which is a bare RET -- the same empty body as GameObject's
    // own slot 96 (LAB_00401e90).
}

unsigned* FClientEquippedObj::Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                      void* Matrix, void* Extra)
{
    // FUN_00533780 -- the body is EquippedObjSubmitDrawRetail (space_scene_obj_draw.cpp, W3-B).
    return EquippedObjSubmitDrawRetail(this, OutLod, Camera, CachedLod, Matrix, Extra);
}

void FClientEquippedObj::Slot198(DamageList const& Damage)
{
    // FUN_005339e0 (RET 4). 0x005339e5..0x00533a02: in multiplayer only, every entry of the
    // list (head's next up to the head) is applied to the object; each arm narrows the object
    // by 0x103 again (null when it is not a CEqObj, used with no test).
    if (!SinglePlayer())
    {
        for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            DamageEntry const& Entry = *It;
            if (Entry.SubObjId == SUBOBJ_ID_ROOT)
            {
                // 0x00533a10..0x00533a50: the hull -> vt+0x98 set_hit_pts(HitPts).
                static_cast<CEqObj*>(NarrowObject(this, 0x103u))->set_hit_pts(Entry.HitPts);
            }
            else if (Entry.SubObjId == SUBOBJ_ID_POWER)
            {
                // 0x00533a55..0x00533a8e: the power -> the CEqObj's +0x198 directly.
                static_cast<CEqObj*>(NarrowObject(this, 0x103u))->Power = Entry.HitPts;
            }
            else if (SubObjectID::IsEquipID(Entry.SubObjId))
            {
                // 0x00533a93..0x00533add: the equipment with that id (CEquipManager::FindByID
                // on +0xe4); none -> the next entry.
                CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
                CEquip* const Equip = EqObj->EquipManager.FindByID(Entry.SubObjId);
                if (Equip == nullptr)
                    continue;
                if (static_cast<int>(Damage.Cause) == 0x13)
                {
                    // 0x00533ae3..0x00533af7: cause 0x13 -> CEShield::ShieldBatteryBoost on
                    // it (no type test).
                    static_cast<CEShield*>(Equip)->ShieldBatteryBoost(Entry.HitPts);
                }
                else if (Equip->Type == 0x10000u &&
                         static_cast<CECargo*>(Equip)->Count > 1u && Entry.HitPts == 0.0f)
                {
                    // 0x00533af9..0x00533b20: cargo (type 0x10000) stacked above one whose
                    // entry is exactly 0 (FCOMP 0.0 / TEST AH,0x44 / JP: only equal falls
                    // through) -> CECargo::RemoveFromStack(1).
                    static_cast<CECargo*>(Equip)->RemoveFromStack(1u);
                }
                else
                {
                    // 0x00533b22..0x00533b2b: otherwise vt+0x40 SetHitPoints(HitPts).
                    Equip->SetHitPoints(Entry.HitPts);
                }
            }
            else
            {
                // 0x00533b2d..0x00533b5d: a collision group -> vt+0xe8
                // set_sub_obj_hit_pts(SubObjId, HitPts).
                static_cast<CEqObj*>(NarrowObject(this, 0x103u))
                    ->set_sub_obj_hit_pts(Entry.SubObjId, Entry.HitPts);
            }
        }
    }

    // 0x00533b70..0x00533b9e: vt+0xfc destroy_sub_objs(Damage, true), in single player too.
    static_cast<CEqObj*>(NarrowObject(this, 0x103u))->destroy_sub_objs(Damage, true);
    // 0x00533ba4..0x00533bcf: CEquipManager::CleanUp on +0xe4.
    static_cast<CEqObj*>(NarrowObject(this, 0x103u))->EquipManager.CleanUp();
}

void FClientEquippedObj::Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // FUN_005340b0 (RET 0x10). B is the hit sub-object id (its low word).
    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    // 0x005340ba..0x005340c8: SubObjectID::IsShieldEquipID.
    if (SubObjectID::IsShieldEquipID(SubObj))
    {
        // 0x005340ca..0x00534114: the body of FUN_00533100 inlined -- the equipment SubObj of
        // the class 0x103 object (no test before +0xe4), then the first wrapper on the +0xb8
        // list whose +4 is it.
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(this, SubObj);
        // 0x00534136..0x00534147: a wrapper whose equipment (no test) has type 0x100 -- a
        // shield.
        if (Wrapper != nullptr && Wrapper->Equip->Type == 0x100u)
        {
            // 0x00534149..0x0053416c: this object's velocity (vtable +4), Other's velocity
            // (+4) and mass (+0x30) are fetched and dropped -- leftovers of the inlined body.
            (void)get_velocity();
            (void)Other->get_velocity();
            (void)Other->get_mass();
            // 0x0053416e..0x0053417d: the wrapper's vtable +0x14 (Contact, Other, D).
            CliShieldEquipFxCollisionHit(Wrapper, Contact, Other, D);
            return;
        }
    }
    // 0x00534116..0x00534128: FUN_00539cd0 with the four arguments.
    FClientGameobj::Slot1a0(Contact, B, Other, D);
}

void FClientEquippedObj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // FUN_00534190 (RET 0xc). B is the hit sub-object id (its low word).
    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    // 0x00534197..0x005341a5: SubObjectID::IsShieldEquipID.
    if (SubObjectID::IsShieldEquipID(SubObj))
    {
        // 0x005341a7..0x005341f1: FUN_00533100 inlined, as in slot 104.
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(this, SubObj);
        // 0x0053420b..0x0053422a: a shield wrapper (equipment type 0x100, no test of the
        // equipment) -> its vtable +0x18 (A, Munition).
        if (Wrapper != nullptr && Wrapper->Equip->Type == 0x100u)
        {
            CliShieldEquipFxMunitionHit(Wrapper, A, Munition);
            return;
        }
    }
    // 0x005341f3..0x00534200: FUN_00539d80 with the three arguments.
    FClientGameobj::Slot1a4(A, B, Munition);
}

void FClientEquippedObj::Slot1a8(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj)
{
    // LAB_005356c0 (RET 0xc): the map's operator[] -- lower_bound on SubObj, a {SubObj,
    // {0, SUBOBJ_ID_NONE}} node inserted at the hint when the key is missing (FUN_00535dd0) --
    // then the value overwritten with the two arguments (0x005357ce..0x005357e2).
    FEquipSubTarget& Entry = SubTargets[SubObj];
    Entry.TargetId = TargetId;
    Entry.TargetSubObj = TargetSubObj;
}

void FClientEquippedObj::ApplySubTarget(unsigned short SubObj)
{
    // FUN_005357f0 (RET 4). 0x005357f8..0x00535836: insert {SubObj, {0, SUBOBJ_ID_NONE}}
    // (FUN_00535b80, no replace), keep the iterator.
    std::pair<std::map<unsigned short, FEquipSubTarget>::iterator, bool> const Inserted =
        SubTargets.insert(std::make_pair(SubObj, FEquipSubTarget()));
    FEquipSubTarget const& Entry = Inserted.first->second;

    // 0x0053583a..0x00535853: no target id, or no Gameobj for it in the ObjDB
    // (FUN_005416c0(id, 0)) -> nothing.
    if (Entry.TargetId == 0)
        return;
    FClientGameobj* const Target = ClientObjDb_FindObject(Entry.TargetId, 0);
    if (Target == nullptr)
        return;

    if (SubObj == SUBOBJ_ID_NONE)
    {
        // 0x00535867..0x005358af: this object's own target (vt+0x74) and sub-target (vt+0x78);
        // unchanged when it is the same object (vt+0x20 ids) on the same sub-object.
        IObjRW* Current = nullptr;
        unsigned short CurrentSub = SUBOBJ_ID_NONE;
        get_target(Current);
        get_subtarget(CurrentSub);
        if (Current != nullptr)
        {
            unsigned const CurrentId = Current->get_id();
            unsigned const TargetGoId = Target->get_id();
            if (CurrentId == TargetGoId && CurrentSub == Entry.TargetSubObj)
                return;
        }
    }
    else
    {
        // 0x005358d7..0x00535957: the gun at SubObj (the object narrowed 0x103, no null test;
        // CEquipManager::FindByID, CEGun::cast) -- none, or not a gun -> nothing; unchanged
        // when its target (CEGun::GetTarget) is the Gameobj's object (+0xb0 id against vt+0x20)
        // on the same sub-object.
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        CEquip* const Equip = EqObj->EquipManager.FindByID(SubObj);
        if (Equip == nullptr)
            return;
        CEGun* const Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            return;
        CSimple* GunTarget = nullptr;
        unsigned short GunSub = SUBOBJ_ID_NONE;
        Gun->GetTarget(GunTarget, GunSub);
        if (GunTarget != nullptr)
        {
            unsigned const TargetGoId = Target->get_id();
            if (GunTarget->Id == TargetGoId && GunSub == Entry.TargetSubObj)
                return;
        }
    }

    // 0x005358b5..0x005358ca / 0x00535959..0x0053596d: IObjAffect (+4) slot 17 (vt+0x44) with
    // (&SubObj, the target Gameobj, the stored sub-object, 0).
    static_cast<IObjAffect*>(this)->slot_44(&SubObj, static_cast<IObjRW*>(Target),
                                            Entry.TargetSubObj, 0);
}

void FClientEquippedObj::Slot1b0(Vector const* Pos)
{
    // FUN_00533be0 (RET 4) -- the explosion of an equipped object.
    // 0x00533be9..0x00533c66: every piece of equipment (CEquipTraverser default ctor,
    // CEquipManager::Traverse on +0xe4 of the object narrowed by 0x103, no null test): an
    // attached one (CAttachedEquip::cast) that is not a shield (type 0x100) gets vt+0x88
    // SetFate(2, separated); every one then gets vt+0x34 Destroy.
    CEquipTraverser Walk;
    for (;;)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        CEquip* const Equip = EqObj->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        CAttachedEquip* const Attached = CAttachedEquip::cast(Equip);
        if (Attached != nullptr && Equip->Type != 0x100u)
            Attached->SetFate(DamageEntry::SubObjFate::Separated);
        Equip->Destroy();
    }

    // 0x00533c68..0x00533cc5: every collision group (the traverser's start byte set to 1
    // inline, CArchGroupManager::Traverse on +0x144) not already destroyed (State 3) gets
    // CArchGroup::Kill(1).
    CArchGrpTraverser Groups;
    for (;;)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        CArchGroup* const Group = EqObj->ArchGroups.Traverse(Groups);
        if (Group == nullptr)
            break;
        if (Group->State == 3)
            continue;
        Group->Kill(DamageEntry::SubObjFate::Destroyed);
    }

    // 0x00533cc7..0x00533cf2: CArchGroupManager::DestroyDeadGroups; 0x00533cf8..0x00533cff:
    // GameObject's slot 108 FUN_0053a880(Pos).
    static_cast<CEqObj*>(NarrowObject(this, 0x103u))->ArchGroups.DestroyDeadGroups();
    FClientGameobj::Slot1b0(Pos);
}

bool FClientEquippedObj::Slot1c0(unsigned SourceId, ID_String const* FuseId,
                                 unsigned short SubObjId, float AtT, float Lifetime)
{
    // FUN_00534240 (RET 0x14) -- its body is HostIgniteFuse (client_equip_fx.cpp).
    return HostIgniteFuse(this, SourceId, *FuseId, SubObjId, AtT, Lifetime);
}

bool FClientEquippedObj::Slot1c4(ID_String const* FuseId, unsigned short SubObjId, float AtT)
{
    // FUN_005343d0 (RET 0xc). 0x005343d4..0x0053440a: the +0xc4 list from the head's next;
    // the loop top leaves once a fuse was found (BL); a node whose Fuse +4 (the name) equals
    // *FuseId gets Fuse::UnBurn(SubObjId, AtT) and sets BL. Returns BL.
    FFuseBurnNode* const Sentinel = static_cast<FFuseBurnNode*>(Fuses.Head);
    bool bFound = false;
    for (FFuseBurnNode* Node = Sentinel->Next; Node != Sentinel && !bFound; Node = Node->Next)
    {
        if (Node->Live.Name().Id == FuseId->Id)
        {
            bFound = true;
            Node->Live.UnBurn(SubObjId, AtT);
        }
    }
    return bFound;
}

bool FClientEquippedObj::Slot1c8()
{
    // FUN_00534420 (bare RET). 0x00534423..0x00534449: the +0xc4 list; a node whose Fuse +0xc
    // (bDeathFuse) is set and which Fuse::IsSpent says is not spent -> true at once; the end
    // of the list -> false.
    FFuseBurnNode* const Sentinel = static_cast<FFuseBurnNode*>(Fuses.Head);
    for (FFuseBurnNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Live.IsDeathFuse() && !Node->Live.IsSpent())
            return true;
    }
    return false;
}

bool FClientEquippedObj::is_dying() const
{
    // LAB_00534860: a tail jump through this object's own vtable +0x1c8 (slot 114).
    return const_cast<FClientEquippedObj*>(this)->Slot1c8();
}

int FClientEquippedObj::scan_cargo(IObjRW* Scanned, EquipDescVector& OutCargo) const
{
    // LAB_00535650 (RET 8). 0x0053565b..0x00535669: IObjInspectImpl::scan_cargo first; its
    // result is returned unchanged.
    int const Result = IObjInspectImpl::scan_cargo(Scanned, OutCargo);
    // 0x00535665..0x00535679: only when this is the player's ship (FUN_00548eb0 on get_id).
    if (IsPlayerShipId(get_id()))
    {
        // 0x0053567b..0x005356aa: the scanned object's id, this id again, then
        // DAT_0067ecd0 vt+0x104 (SPScanCargo) with DAT_00673344.
        unsigned const ScannedId = Scanned->get_id();
        unsigned const SelfId = get_id();
        LocalServer()->SPScanCargo(SelfId, ScannedId, GetPlayerConnectionId());
    }
    return Result;
}

unsigned char FClientEquippedObj::Slot1cc(FCliEquipFirePacket const* Packet)
{
    // FUN_00534460 -- ported as EquippedObjHandleEquipMsg (cli_projectile_fx.cpp), which reads
    // the gameobj through its fire-host view (+0x10 object, +0xb8 equip-FX list).
    return EquippedObjHandleEquipMsg(this, Packet);
}

int FClientEquippedObj::slot_34(unsigned short const* EquipIds, unsigned Count,
                                Vector const* TargetPos, int SourceClient)
{
    // FUN_00534870 -- ported as CliShipFireWeapons (cli_projectile_fx.cpp), which takes the
    // IObjAffect sub-object (this + 4) as retail's adjustor this.
    return static_cast<int>(CliShipFireWeapons(static_cast<IObjAffect*>(this), EquipIds, Count,
                                               TargetPos, SourceClient));
}

namespace
{
    // The tractor slots' target narrowing: IObjInspect slot 84 cobject, kept only when its class
    // carries all of 0x803 (CLoot), null otherwise (0x00534f08..0x00534f28 / 0x005350c4..0x005350e3).
    CLoot* TractorLootOf(IObjRW* Target)
    {
        CObject* const Obj = const_cast<CObject*>(Target->cobject());
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x803u) != 0x803u)
            return nullptr;
        return static_cast<CLoot*>(Obj);
    }

    // The IObjAffect slots' "local request" test (0x00534e81..0x00534e9b): SourceClient -1, or
    // the player's own connection (FUN_0054bb00 = DAT_00673344).
    bool IsLocalRequest(int SourceClient)
    {
        return SourceClient == -1 ||
               static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    }
}

int FClientEquippedObj::slot_3c(unsigned short const* EquipId,
                                std::vector<IObjRW*> const& Targets, int SourceClient)
{
    // LAB_00535010 (thiscall on the IObjAffect sub-object, RET 0xc) -- listing
    // 0x00535010..0x005351c2 (raw decode; Ghidra left the body as data).
    // 0x0053502b..0x00535064: local request; single player refuses a remote one with -5.
    const bool bLocal = IsLocalRequest(SourceClient);
    if (SinglePlayer() && !bLocal)
        return -5;

    // 0x00535064..0x005350aa: the object narrowed by 0x103 (no null test before the +0xe4
    // LEA), CEquipManager::FindByID(*EquipId); none -> -2.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
    CEquip* const Equip = EqObj->EquipManager.FindByID(*EquipId);
    if (Equip == nullptr)
        return -2;

    // 0x005350b0..0x005350fe: every target (not null-tested) through CETractor::AddTarget on
    // the equipment as found -- no cast, no destroyed test; a 0 (TractorOk) counts.
    CETractor* const Tractor = static_cast<CETractor*>(Equip);
    int Accepted = 0;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
    {
        if (Tractor->AddTarget(TractorLootOf(*It)) == TractorOk)
            Accepted = Accepted + 1;
    }
    // 0x00535100: none accepted (or no targets) -> -4 (signed JLE).
    if (Accepted <= 0)
        return -4;

    // 0x0053510a..0x00535112: a remote request stops here with 0.
    if (!bLocal)
        return 0;

    // 0x00535118..0x0053519c: XTractorObjects {the gameobj's get_id (vt+0x20), *EquipId, the
    // get_id of EVERY target, accepted or not} to DAT_0067ecd0 slot 5 TractorObjects
    // (DAT_00673344, &Info), no null test; the id vector's buffer freed after.
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.Pad_0x06 = 0;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
        Info.TargetIds.push_back((*It)->get_id());
    LocalServer()->TractorObjects(GetPlayerConnectionId(), Info);
    return 0;
}

int FClientEquippedObj::slot_40(unsigned short const* EquipId, IObjRW* Target, int SourceClient)
{
    // FUN_00534e60 (thiscall on the IObjAffect sub-object, RET 0xc) -- 0x00534e60..0x00534fe6,
    // raw decode (Ghidra left it undisassembled). The single-target form of slot 15.
    // 0x00534e7b..0x00534eae: local request; single player refuses a remote one with -5.
    const bool bLocal = IsLocalRequest(SourceClient);
    if (SinglePlayer() && !bLocal)
        return -5;

    // 0x00534eb4..0x00534eba: no target -> -4.
    if (Target == nullptr)
        return -4;

    // 0x00534ec0..0x00534f02: the object narrowed by 0x103, FindByID(*EquipId); none -> -2.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
    CEquip* const Equip = EqObj->EquipManager.FindByID(*EquipId);
    if (Equip == nullptr)
        return -2;

    // 0x00534f08..0x00534f35: CETractor::AddTarget on the equipment as found; any failure
    // code -> -5.
    if (static_cast<CETractor*>(Equip)->AddTarget(TractorLootOf(Target)) != TractorOk)
        return -5;

    // 0x00534f3b..0x00534f43: a remote request stops here with 0.
    if (!bLocal)
        return 0;

    // 0x00534f45..0x00534fb6: XTractorObjects {get_id (vt+0x20), *EquipId, [Target's get_id]}
    // to DAT_0067ecd0 slot 5 TractorObjects(DAT_00673344, &Info); the buffer freed after.
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.Pad_0x06 = 0;
    Info.TargetIds.push_back(Target->get_id());
    LocalServer()->TractorObjects(GetPlayerConnectionId(), Info);
    return 0;
}

int FClientEquippedObj::slot_44(unsigned short const* EquipId, IObjRW* Target,
                                unsigned short SubTarget, int SourceClient)
{
    // FUN_005351d0 -- ported as CliEquippedObjSetGunTarget (cli_projectile_fx.cpp).
    return CliEquippedObjSetGunTarget(static_cast<IObjAffect*>(this), EquipId, Target, SubTarget,
                                      SourceClient);
}

int FClientEquippedObj::slot_4c(unsigned short const* EquipId, bool bActivate, int SourceClient)
{
    // LAB_00535330 -- ported as CliShipActivateEquip (cli_projectile_fx.cpp); its value is
    // retail's EAX (0 on every path, 0x00535459).
    return CliShipActivateEquip(static_cast<IObjAffect*>(this), EquipId,
                                static_cast<unsigned char>(bActivate), SourceClient);
}

int FClientEquippedObj::slot_58(unsigned short EquipId, unsigned Count, int SourceClient)
{
    // LAB_00535470 (thiscall on the IObjAffect sub-object, RET 0xc) -- listing
    // 0x00535470..0x00535646: use a repair kit / shield battery from the cargo hold.
    // 0x00535475..0x0053548f: only a local request (SourceClient -1 or DAT_00673344) goes on;
    // anything else -> -5. There is no single-player test here.
    if (!IsLocalRequest(SourceClient))
        return -5;

    // 0x00535495..0x005354dc: the object narrowed by 0x103 (no null test before the +0xe4
    // LEA), CEquipManager::FindByID(EquipId), CECargo::cast of the result.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
    CECargo* const Cargo = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
    {
        // 0x00535601..0x00535626: EquippedObj.cpp line 0x540, then -2.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\EquippedObj.cpp", 0x540,
              "EqObj::use_item() couldn't find target item on Client request.");
        return -2;
    }

    // 0x005354e2..0x005354ef: the amount -- the stack's count (+0x28) or Count, the smaller
    // (unsigned JB).
    const unsigned UseCount = (Cargo->Count < Count) ? Cargo->Count : Count;

    // 0x005354f1..0x00535505: the cargo archetype's class type (+0xc, Root slot 4).
    const Archetype::AClassType ClassType = Cargo->Arch->get_class_type();
    if (ClassType == Archetype::AClassRepairKit)
    {
        // 0x00535586..0x005355be: a repair kit only while the hull is below full --
        // CSimple::get_relative_health() < 1.0 (TEST AH,5 / JP: equal, above or unordered
        // skip), on the object narrowed again (no null test).
        CEqObj* const Hull = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        if (!(Hull->get_relative_health() < 1.0f))
            return 0;
    }
    else if (ClassType == Archetype::AClassShieldBattery)
    {
        // 0x0053550b..0x00535584: a shield battery only while some shield (CEquipTraverser
        // 0x100) is below full and has hit points at all: vt+0x44 GetRelativeHealth < 1.0
        // (TEST AH,5 / JP) and vt+0x38 GetMaxHitPoints > 0.0 (TEST AH,0x41 / JNZ). The object is
        // re-narrowed on every step, as retail does.
        CEquipTraverser Walk(static_cast<int>(EquipTypeShield));
        for (;;)
        {
            CEqObj* const Owner = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
            CEquip* const Shield = Owner->EquipManager.Traverse(Walk);
            if (Shield == nullptr)
                return 0;
            if (!(Shield->GetRelativeHealth() < 1.0f))
                continue;
            if (Shield->GetMaxHitPoints() > 0.0f)
                break;
        }
    }
    else
    {
        // 0x00535505 -> 0x0053562e: any other cargo -> 0.
        return 0;
    }

    // 0x005355c0..0x005355f3: SSPUseItem {get_id (vt+0x20), EquipId, the amount} to
    // DAT_0067ecd0 slot 56 SPRequestUseItem(&Info, DAT_00673344), no null test; 0.
    SSPUseItem Info;
    Info.ObjectId = get_id();
    Info.ItemId = EquipId;
    Info.Count = static_cast<unsigned short>(UseCount);
    LocalServer()->SPRequestUseItem(Info, GetPlayerConnectionId());
    return 0;
}

// ===========================================================================
// Solar
// ===========================================================================

FClientSolarGameobj::FClientSolarGameobj()
    : FClientEquippedObj(EClientGameobjKind::Solar)
{
    // FUN_005475b0: EquippedObj's ctor (kind 2), then its own members. +0xf0..+0x1ef are not
    // written.
    Field_0xe4 = 0;
    DockScript.Length = 0;
    DockScript.Data[0] = '\0';
    Field_0x1f0 = 1;
    BurnColor[0] = 1.0f;
    BurnColor[1] = 1.0f;
    BurnColor[2] = 1.0f;
    Field_0x200.clear();                                                       // 0x00547611
    Field_0x204 = nullptr;
}

FClientSolarGameobj::~FClientSolarGameobj()
{
    // FUN_00547680.
    // 0x005476bf..005476de: the JumpGateEffect at +0x204, when set -- its destructor
    // FUN_00502e40 (0x005476d3) and the global operator delete FUN_005b7e1d. The field is not
    // written back.
    if (Field_0x204 != nullptr)
        delete static_cast<FJumpGateEffect*>(Field_0x204);
    // 0x005476e1..00547730: with an object, the CEqObj (0x103) gets vt+0x94
    // disconnect(IObjDB* = &DAT_00678a2c) and vt+0xbc disconnect(INotify* = this+0xac); the
    // narrowing has no null test on the result.
    if (Object != nullptr)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        EqObj->disconnect(GetClientObjDb());
        static_cast<CEqObj*>(NarrowObject(this, 0x103u))->disconnect(static_cast<INotify*>(this));
    }
    // 0x0054773d: FUN_00532d40, EquippedObj's destructor, runs next.
}

// FClientSolarGameobj::notify (FUN_005478f0) is defined after the ship's, next to the
// createeffect helpers both use.

void FClientSolarGameobj::create(Archetype::Root* Arch)
{
    // FUN_005477d0.
    CreateOpenConnect(this, CObject::ClassSolar, Arch);
}

bool FClientSolarGameobj::find(unsigned const& Id)
{
    // FUN_00547760.
    return FindAndConnect(this, CObject::ClassSolar, Id);
}

void FClientSolarGameobj::Slot170()
{
    // FUN_00547850 (bare RET). 0x00547853: EquippedObj's slot 92 (FUN_00533e10), called
    // directly; 0x00547858..0x0054786d: the nebula at get_position (vt+0) into +0x38 and its
    // fog factor into +0x3c (FUN_005553b0, no hint).
    FClientEquippedObj::Slot170();
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), reinterpret_cast<float*>(&DrawStateB), nullptr);
    DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));
}

void FClientSolarGameobj::update(float Dt)
{
    // FUN_00547880 (RET 4). 0x00547884..0x005478c0: in multiplayer only, the CSolar (0x303,
    // narrowed with no test of the result) runs the dumb CObject::update (vt+0x50).
    if (!SinglePlayer())
        NarrowObject(this, 0x303u)->update(Dt, CObject::UPDATE_DUMB);

    // 0x005478c3..0x005478c6: EquippedObj's update, called directly.
    FClientEquippedObj::update(Dt);

    // 0x005478cb..0x005478d6: the jump gate effect, when there is one (FUN_00502f00).
    if (Field_0x204 != nullptr)
        static_cast<FJumpGateEffect*>(Field_0x204)->Update(Dt);

    // 0x005478db..0x005478e8: the star, when there is one, through its slot 2 (+8,
    // LAB_00511690, RET 4).
    if (Field_0xe4 != nullptr)
    {
        typedef void(__thiscall * FStarUpdate)(FStarSphere*, float);
        Field_0xe4->UpdateIntensity(Dt);
    }
}

namespace
{
    // DAT_00678b88 -- solar.cpp's 3DMathEngine, acquired (FUN_005b73e0) on first use; its only
    // site is the radius slot 0x005481bd..0x005481cb.
    I3DMathEngine* G_SolarMath = nullptr;
}

float FClientSolarGameobj::Slot178(Vector& Center)
{
    // LAB_00548110 (RET 4) -- the solar's radius about Center.
    // 0x00548116..0x00548149: with a star (+0xe4): Center = the star owner's position
    // (FUN_005125f0), the radius is the star's glow extent (FUN_00512620).
    if (Field_0xe4 != nullptr)
    {
        Vector StarPos;
        StarSphereGetOwnerPosition(Field_0xe4, &StarPos);
        Center = StarPos;
        return StarSphereMaxGlowScale(Field_0xe4);
    }

    // 0x00548150..0x00548178: a planet / star / moon type (CSolar TypeBits +0xe0 & 7; the
    // object narrowed by 0x303, no test) -> the engine instance's extent: IEngine vt+0x88
    // GetInstanceExtent(get_index, 0, &Radius, &Center) with Radius zeroed first, then Center
    // taken to world space by the object's transform (vt+0x10) through solar.cpp's math
    // engine slot 10 TransformXform into a local, copied back.
    CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, 0x303u));
    if ((Simple->TypeBits & 7u) != 0)
    {
        float Radius = 0.0f;
        long const Inst = get_index();
        DALib::Engine->GetInstanceExtent(reinterpret_cast<void*>(static_cast<intptr_t>(Inst)), 0u,
                                         &Radius, &Center.x);
        FTransform const& Xform = reinterpret_cast<FTransform const&>(get_orientation());
        if (G_SolarMath == nullptr)
            G_SolarMath = Acquire3DMathEngine();
        FVector World;
        G_SolarMath->TransformXform(World, Xform, reinterpret_cast<FVector const&>(Center));
        Center = reinterpret_cast<Vector const&>(World);
        return Radius;
    }

    // 0x005481fe..0x00548205: GameObject's slot 94 (FUN_00539810).
    return FClientGameobj::Slot178(Center);
}

unsigned* FClientSolarGameobj::Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                       void* Matrix, void* Extra)
{
    // LAB_00548220 (RET 0x14) -- the solar draw. 0x00548227..0x00548233: the local LOD starts
    // {0, 0xffff}, and every path that draws nothing returns it (0x005484a9).
    unsigned Lod = 0xffff0000u;
    CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, 0x303u));

    // 0x0054825b..0x00548299: a star -> only when option 0x24 is on: the sphere's slot 3
    // (DrawSimple, +0xc) with the camera, and {4, 0xffff}.
    if (Field_0xe4 != nullptr)
    {
        if (GetGameOption(0x24) == 0)
        {
            *OutLod = Lod;
            return OutLod;
        }
        typedef void(__thiscall * FStarDrawSimple)(FStarSphere*, void*);
        Field_0xe4->DrawSimple(Camera);
        *OutLod = 0xffff0004u;
        return OutLod;
    }

    unsigned const TypeBits = Simple->TypeBits;
    bool bSubmit = false;
    if ((TypeBits & 7u) != 0)
    {
        // 0x0054829c..0x005482b2: planet / star / moon -> drawn only with option 0x23 on.
        if (GetGameOption(0x23) == 0)
        {
            *OutLod = Lod;
            return OutLod;
        }
        bSubmit = true;
#ifdef FREELANCER_RP9
        // Port-only: the far bucket draws planets without depth; the Direct3D 9 volumetric
        // nebula ends its rays on their bounding sphere instead (nebula_volume.cpp). Only while
        // it is active.
        if (NebulaVolume::IsActive())
        {
            Vector Centre;
            const float Radius = Slot178(Centre);
            NebulaVolume::NoteOpaqueSphere(&Centre.x, Radius);
        }
#endif
    }
    else if ((TypeBits & 8u) != 0)
    {
        // 0x005482e8..0x005482ea: bit 3 -> nothing.
        *OutLod = Lod;
        return OutLod;
    }
    else if (((TypeBits >> 9) & 1u) == 0)
    {
        // 0x00548311..0x0054831d: not a waypoint solar (bit 9) -> the ordinary draw.
        bSubmit = true;
    }

    if (bSubmit)
    {
        // 0x005482b8..0x005482e5: EquippedObj's draw FUN_00533780 into a temporary (the
        // Camera argument's stack slot), copied to *OutLod.
        unsigned Temp = 0;
        unsigned* const Result =
            EquippedObjSubmitDrawRetail(this, &Temp, Camera, CachedLod, Matrix, Extra);
        *OutLod = *Result;
        return OutLod;
    }

    // 0x0054831f..0x00548352: the waypoint marker -- only while the software cursor is drawn
    // (FUN_0041dd90) and the HUD_Waypoint (DAT_00674bc8) exists and is active (FUN_004f4d20);
    // its fade (FUN_004f4ca0) is fetched before the active test.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        *OutLod = Lod;
        return OutLod;
    }
    HUD_Waypoint* const Waypoint = GetHudWaypoint();
    if (Waypoint == nullptr)
    {
        *OutLod = Lod;
        return OutLod;
    }
    bool const bActive = GetHudWaypoint()->IsActive();
    float const Fade = GetHudWaypoint()->GetScale();
    if (!bActive)
    {
        *OutLod = Lod;
        return OutLod;
    }

    // 0x00548358..0x005483d8: the colour is the palette entry DAT_00679b80 and the size 1.0;
    // with a player ship, a distance above 2500 (DAT_005db7d8; FCOM / TEST AH,0x41 / JNZ)
    // gives size = distance * 0.0005 (DAT_005e04d8), capped at 99 (DAT_005e04d4 -> 0x42c60000).
    unsigned long Color = G_NnPaletteArgb_679b80;
    float Size = 1.0f;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        Vector const& PlayerPos = Player->get_position();
        Vector const& Pos = get_position();
        double const Dx = static_cast<double>(Pos.x) - PlayerPos.x;
        double const Dy = static_cast<double>(Pos.y) - PlayerPos.y;
        double const Dz = static_cast<double>(Pos.z) - PlayerPos.z;
        double const Distance = std::sqrt((Dy * Dy + Dx * Dx) + Dz * Dz);
        if (Distance > 2500.0)
        {
            double const Scaled = Distance * static_cast<double>(0.0005f);
            Size = static_cast<float>(Scaled);
            if (Scaled > 99.0)
                Size = 99.0f;
        }
    }

    // 0x005483d8..0x00548426: the colour's alpha byte scaled by the fade (FILD * float, ftol);
    // FUN_00433f40(Camera, get_index, 1, &colour, 0, size, 0, 0) -- its answer is the new LOD
    // {answer, 0xffff}.
    unsigned char ColorBytes[4];
    std::memcpy(ColorBytes, &Color, sizeof(ColorBytes));
    ColorBytes[3] = static_cast<unsigned char>(
        static_cast<long>(static_cast<double>(ColorBytes[3]) * Fade));
    long const Inst = get_index();
    int const Answer = SubmitWin3DMaterialEngineInstance(
        Camera, reinterpret_cast<void*>(static_cast<intptr_t>(Inst)), 1, ColorBytes, 0, Size, 0,
        0u);
    Lod = 0xffff0000u | static_cast<unsigned short>(Answer);

    // 0x00548429..0x0054845c: an answer below 3 (unsigned) or a solar that is no longer a
    // waypoint (bit 9, re-read) returns that LOD.
    if (static_cast<unsigned short>(Answer) < 3 ||
        ((static_cast<CSimple*>(NarrowObject(this, 0x303u))->TypeBits >> 9) & 1u) == 0)
    {
        *OutLod = Lod;
        return OutLod;
    }

    // 0x0054845e..0x005484a6: the outline pass -- {0xff, 0x80, 0xff, ftol(fade * 255)}
    // (DAT_005c75d8) through FUN_00434630(Camera, get_index, 1, &colour, 0, size, 0).
    unsigned char Outline[4];
    Outline[0] = 0xff;
    Outline[1] = 0x80;
    Outline[2] = 0xff;
    Outline[3] = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Fade) * 255.0f));
    long const OutlineInst = get_index();
    SubmitWin3DTintedEngineInstanceWrapped(
        Camera, reinterpret_cast<void*>(static_cast<intptr_t>(OutlineInst)), 1, Outline, 0, Size,
        0u);
    *OutLod = Lod;
    return OutLod;
}

void FClientSolarGameobj::Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                                  void const* D)
{
    // LAB_00548520 (RET 0x10): a solar of type bits 0..3 (TypeBits +0xe0 & 0xf; the object
    // narrowed by 0x303, no test) ignores the hit; any other goes on to EquippedObj's slot 104
    // (a tail JMP to FUN_005340b0 with the arguments unchanged).
    if ((static_cast<CSimple*>(NarrowObject(this, 0x303u))->TypeBits & 0xfu) != 0)
        return;
    FClientEquippedObj::Slot1a0(Contact, B, Other, D);
}

void FClientSolarGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_00548570 (RET 0xc): the same test, then a tail JMP to FUN_00534190.
    if ((static_cast<CSimple*>(NarrowObject(this, 0x303u))->TypeBits & 0xfu) != 0)
        return;
    FClientEquippedObj::Slot1a4(A, B, Munition);
}

void FClientSolarGameobj::Slot1b0(Vector const* Pos)
{
    // LAB_005485b0 (RET 4): a bare JMP to EquippedObj's slot 108 (FUN_00533be0).
    FClientEquippedObj::Slot1b0(Pos);
}

void FClientSolarGameobj::ReadIni(INI_Reader& Reader)
{
    // FUN_005485c0 (thiscall, RET 4).
    // 0x005485ca: the EquippedObj body FUN_00533e10, called directly (not the solar's own
    // slot 92).
    FClientEquippedObj::Slot170();

    // 0x005485d8..0x00548862: every value of the (rewound) section; the first key that matches
    // wins, unknown keys are skipped.
    while (Reader.read_value())
    {
        if (Reader.is_value("ring"))
        {
            // 0x00548601..0x00548683: value 0 into a TString<0x20> (the zone), value 1 into a
            // TString<0x104> (the ini), both through FUN_00417c50 (a null value leaves them
            // empty). A new TexturePanels (FUN_0050bfa0) loads (ini, zone) through its slot 1;
            // nothing keeps the pointer but the panels' global list.
            TString<0x20> Zone;
            Zone = Reader.get_value_string(0);
            TString<0x104> File;
            File = Reader.get_value_string(1);
            TexturePanelsCreate()->Load(File.Data, Zone.Data);
        }
        else if (Reader.is_value("burn_color"))
        {
            // 0x00548699..0x0054871a: retail tests "burn_color" a second time (always true) and
            // reads value 1, value 0, value 2; each / 255 (DAT_005c7f68) into +0x1f4.
            float const Green = Reader.get_value_float(1);
            float const Red = Reader.get_value_float(0);
            float const Blue = Reader.get_value_float(2);
            float const kInv255 = 1.0f / 255.0f; // DAT_005c7f68 (0x3b808081)
            BurnColor[0] = Red * kInv255;
            BurnColor[1] = Green * kInv255;
            BurnColor[2] = Blue * kInv255;
        }
        else if (Reader.is_value("dock_script"))
        {
            // 0x0054872c..0x00548789: the inlined TString<0x104> assign into +0xe8 (a null value
            // gives length 0 and an empty string; otherwise clamped to 0x103).
            DockScript = Reader.get_value_string();
        }
        else if (Reader.is_value("voice"))
        {
            // 0x0054879b..0x005487da: the object narrowed to CSolar (0x303) -- retail writes
            // through the result with no null test -- gets +0x13c = CreateID(value).
            char const* const Voice = Reader.get_value_string();
            CSolar* const Solar = static_cast<CSolar*>(NarrowObject(this, 0x303u));
            Solar->VoiceId = CreateID(Voice);
        }
        else if (Reader.is_value("jump_effect"))
        {
            // 0x005487e9..0x005487ff: +0x200 = CreateID(value).
            Field_0x200.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("star"))
        {
            // 0x0054880c..0x00548855: +0xe4 = FUN_0050c630(this) (a listed star), then its slot 1
            // Load(GetFileSysPath("solar\\stararch.ini", DALib::Data), value).
            Field_0xe4 = StarSphereCreateListed(this);
            char Path[0x104];
            GetFileSysPath(Path, "solar\\stararch.ini", DALib::Data);
            using FStarLoad = int(__thiscall*)(FStarSphere*, char const*, char const*);
            Field_0xe4->Load(Path, Reader.get_value_string());
        }
    }

    // 0x0054886a..0x00548880: the nebula at the solar's position into +0x38, its fog factor
    // into +0x3c (FUN_005553b0, no hint).
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), reinterpret_cast<float*>(&DrawStateB), nullptr);
    DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));
}

// ===========================================================================
// Ship
// ===========================================================================

FClientShipGameobj::FClientShipGameobj()
    : FClientEquippedObj(EClientGameobjKind::Ship)
{
    // FUN_00543380: EquippedObj's ctor (kind 1), then its own members in store order.
    Field_0xe4 = 0;
    Field_0xe8 = 0;
    Field_0xec = nullptr;
    Field_0xf0 = nullptr;
    Field_0xf4[0] = nullptr;
    Field_0xf4[1] = nullptr;
    Field_0xf4[2] = nullptr;
    Field_0xf4[3] = nullptr;
    Field_0x104 = nullptr;                                                     // 0x005433e6
    MeshHolderConstruct(&Field_0x108);                                         // FUN_00420d40
    Field_0x118.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Field_0x118.Translation.x = 0.0f;                                          // 0x0054342c..3e
    Field_0x118.Translation.y = 0.0f;
    Field_0x118.Translation.z = 0.0f;
    Field_0x148 = nullptr;
    Field_0x14c = nullptr;
    Field_0x150 = 0;
    Field_0x154 = 0;
    Field_0x15c = 0;
    // 0x0054345c..0x00543492: +0x160 .. +0x184 zeroed, one dword at a time.
    Field_0x160 = 0;
    int CompanionSlot = 0;
    for (CompanionSlot = 0; CompanionSlot < 4; ++CompanionSlot)
        Field_0x164[CompanionSlot] = nullptr;
    for (CompanionSlot = 0; CompanionSlot < 4; ++CompanionSlot)
        Field_0x174[CompanionSlot] = nullptr;
    Field_0x184 = 0;
    Field_0x18c = static_cast<unsigned char>(Field_0x18c & 0xfcu);             // 0x00543498
    Field_0x188 = nullptr;
    Field_0x18c = 0;                                                           // 0x005434a5
    Pad18d[0] = 0;
    Pad18d[1] = 0;
    Pad18d[2] = 0;
    Field_0x158 = 0;                                                           // 0x005434c9
    // 0x005434cf..005434e9: +0x34 = rand() * (1 / 32768) (DAT_005caeec).
    Field_0x34 = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05);
}

FClientShipGameobj::~FClientShipGameobj()
{
    // FUN_005444a0. It runs with ESI = this + 0xc (the CBase sub-object: [ESI+0x4] is the
    // object at +0x10), so every ESI-relative offset in the listing is 0xc below the gameobj
    // offset used here.
    // 0x005444e2..0x005444f0: FUN_00420f70 on the mesh holder (+0x108).
    ClientMeshHolderRelease(&Field_0x108);
    // 0x005444f5..0x00544507: the archetype at +0x104, Archetype::Root::Release.
    if (Field_0x104 != nullptr)
    {
        Field_0x104->Release();
        Field_0x104 = nullptr;
    }
    // 0x0054450d..0x00544519: +0x148's vt+0x10, the pointer left as it is.
    if (Field_0x148 != nullptr)
    {
        Field_0x148->Release();
    }
    // 0x0054451c..0x0054455e: the two sounds (+0x14c, +0xf0) -- vt+0x60 stop, vt+8 release.
    if (Field_0x14c != nullptr)
    {
        Field_0x14c->Stop();
        Field_0x14c->Release();
        Field_0x14c = nullptr;
    }
    if (Field_0xf0 != nullptr)
    {
        Field_0xf0->Stop();
        Field_0xf0->Release();
        Field_0xf0 = nullptr;
    }
    // 0x0054455f..0x00544579: the 0xdc-byte jump effect at +0x188 -- when set, its destructor
    // FUN_00503a80 (0x0054456b), the global operator delete FUN_005b7e1d, and the field zeroed.
    if (Field_0x188 != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(Field_0x188);
        Field_0x188 = nullptr;
    }
    // 0x0054457f..0x00544610: with an object -- in single player only for a ship the local
    // player owns (the class & 3 object's +0xb4 OwnerPlayer, FUN_0054bb00 = DAT_00673344 when
    // the narrow fails, against FUN_0054bb00), in multiplayer always -- the CEqObj (0x103, no
    // null test) gets vt+0x94 disconnect(IObjDB* = &DAT_00678a2c); then, always, vt+0xbc
    // disconnect(INotify* = this + 0xac).
    if (Object != nullptr)
    {
        bool bDisconnectDb = true;
        if (SinglePlayer())
        {
            CObject* const Owned = NarrowObject(this, 0x3u);
            unsigned const Owner = (Owned != nullptr)
                                       ? static_cast<CSimple const*>(Owned)->OwnerPlayer
                                       : GetPlayerConnectionId();
            bDisconnectDb = Owner == GetPlayerConnectionId();
        }
        if (bDisconnectDb)
            static_cast<CEqObj*>(NarrowObject(this, 0x103u))->disconnect(GetClientObjDb());
        static_cast<CEqObj*>(NarrowObject(this, 0x103u))->disconnect(static_cast<INotify*>(this));
    }
    // 0x00544613..0x0054468c: the +0x160 member's destructor (FUN_00544710, inlined) --
    // companion +0x174, then companion +0x164, each: particle (FUN_004f7a90), sound (vt+8),
    // light (FUN_00537ad0), arch slot zeroed -- FUN_00502d90's sequence.
    CliEffectCompanionClear(Field_0x174);
    CliEffectCompanionClear(Field_0x164);
    // 0x00544686..0x00544694: FUN_00420d10 on +0x108 only restores the holder's vptr.
    // 0x00544699..0x005446e0: the companion at +0xf4 -- particle +0xfc, sound +0xf8, light
    // +0x100, then +0xf4 zeroed.
    CliEffectCompanionClear(Field_0xf4);
    // 0x005446ee: FUN_00532d40, EquippedObj's destructor, runs next.
}

namespace
{
    char const kCreateEffectSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\createeffect.h";

    // EffectArch's constructor / destructor (FUN_0040e250 / FUN_0040e2a0), used for the stack
    // archetypes the notify handlers build, live with the registry in vis_effect_arch.cpp.
    // The stack archetype is EH state 0 of those frames (FUN_00545670: [ESP+0xf8] = 0 at
    // 0x005457c9 right after FUN_0040e250, -1 at 0x005459aa before FUN_0040e2a0), so its
    // destructor also runs on the unwind path: hence a scoped object.
    struct FScopedEffectArch
    {
        FScopedEffectArch()
        {
            EffectArchConstruct(&Arch);
        }
        ~FScopedEffectArch()
        {
            EffectArchDestruct(&Arch);
        }
        FEffectArch Arch;

    private:
        FScopedEffectArch(FScopedEffectArch const&) = delete;
        FScopedEffectArch& operator=(FScopedEffectArch const&) = delete;
    };

    // The createeffect.h companion {Arch, sound, particle, light} (FUN_005154a0), EH state 1
    // of the same frames (FUN_00545670: byte [ESP+0xf8] = 1 at 0x005458b1, back to 0 at
    // 0x00545996 before FUN_00502d90): cleared by FUN_00502d90 on both paths.
    struct FScopedEffectCompanion
    {
        explicit FScopedEffectCompanion(FEffectArch* Arch)
        {
            Slots[0] = Arch;
            Slots[1] = nullptr;
            Slots[2] = nullptr;
            Slots[3] = nullptr;
        }
        ~FScopedEffectCompanion()
        {
            CliEffectCompanionClear(Slots); // FUN_00502d90
        }
        void* Slots[4];

    private:
        FScopedEffectCompanion(FScopedEffectCompanion const&) = delete;
        FScopedEffectCompanion& operator=(FScopedEffectCompanion const&) = delete;
    };

    // FUN_00544230 (createeffect.h; thiscall on the effect factory DAT_00674c60, which it does
    // not read; RET 8) -- start whatever the companion holds with Arch, drop what will not
    // start, and report whether anything is still alive.
    bool CreateEffectStart(void* Companion16, FEffectArch* Arch)
    {
        void** const Companion = static_cast<void**>(Companion16);

        // 0x00544235..0x00544252: the particle's slot 0 (Start); a refusal releases it.
        FParticleEffect* const Particle = static_cast<FParticleEffect*>(Companion[2]);
        if (Particle != nullptr && ParticleEffectStart(Particle, Arch) == 0)
        {
            ParticleEffectRelease(Particle);
            Companion[2] = nullptr;
        }

        // 0x00544259..0x00544292: looping from bit 0 of +0x50, then Start(SndEffect, 0); no
        // sound archetype or a refusal releases the source.
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
        if (Sound != nullptr)
        {
            Sound->SetLooping(static_cast<unsigned char>(Arch->Flags50 & 1u));
            if (Arch->SndEffect == nullptr || Sound->Start(Arch->SndEffect, 0) == 0)
            {
                Sound->Release();
                Companion[1] = nullptr;
            }
        }

        // 0x00544299..0x00544330: type 1 with bit 0, colour (+0x4c), radius (+0x48), then
        // Enable (vt+4). Failure warns (line 0xce) and releases; otherwise a named animation
        // (+0x1c length, +0x20 name) is attached -- the lookup result is not null-tested.
        FGameLight* const Light = static_cast<FGameLight*>(Companion[3]);
        if (Light != nullptr)
        {
            EngineLightSetType((FEngineLight*)Light, 1, true);
            EngineLightSetColorBytesAlt((FEngineLight*)Light, &Arch->LgtColorR);
            EngineLightSetRadius((FEngineLight*)Light, Arch->LgtRadius);
            if (!GameLightEnable((FEngineLight*)Light))
            {
                ID_String Name;
                Name.Id = Arch->NicknameId;
                FDUMP(0x100002, "%s(%d) : *** WARNING: couldnt open light",
                      kCreateEffectSourceFile, 0xce, Name.get_string());
                GameLightRelease(Light);
                Companion[3] = nullptr;
            }
            else if (Arch->LgtEffectLen != 0)
            {
                GameLightAttachAnim(Light, GameLightFindAnimByName(Arch->LgtEffect),
                                    Arch->LgtRangeScale, Arch->LgtTimeScale, 0.0f);
            }
        }

        // 0x00544333..0x0054437b: alive -- a particle without the deactivated bit, a sound
        // whose IsActive says so, or a light with no animation keys.
        FParticleEffect* const LiveParticle = static_cast<FParticleEffect*>(Companion[2]);
        if (LiveParticle != nullptr && (LiveParticle->Flags08 & 2u) == 0u)
            return true;
        ISoundSource* const LiveSound = static_cast<ISoundSource*>(Companion[1]);
        if (LiveSound != nullptr && LiveSound->IsActive() != 0)
            return true;
        FGameLight* const LiveLight = static_cast<FGameLight*>(Companion[3]);
        if (LiveLight != nullptr && LiveLight->Anim.Keys == nullptr)
            return true;
        return false;
    }

    // The createeffect.h sequence the notify handlers inline (ship 0x00545883..0x005459a3,
    // solar 0x00547abd..0x00547baa / 0x00547cd5..0x00547dc4 / 0x00547f72..0x00548060). Only
    // with EFFECTS on: the companion {Arch, 0, 0, 0} (FUN_005154a0; the archetype is not
    // AddRef'd), the factory's (DAT_00674c60) particle / sound / light as the archetype asks,
    // attached at EngineIndex with Pose (FUN_004f7d20 / sound vt+0x94 / FUN_005377c0 -- out of
    // line as FUN_005480c0 in the solar copies), started (FUN_00544230), and on success copied
    // into Go's effect list (FUN_004fcea0) -- slot 0 there keeps the address of the caller's
    // stack archetype, exactly as retail's does. The local companion is cleared
    // (FUN_00502d90) either way.
    void CreateEffectOnGameobj(FClientGameobj* Go, FEffectArch* Arch, long EngineIndex,
                               float const* Pose)
    {
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;

        FScopedEffectCompanion Scoped(Arch); // FUN_005154a0; FUN_00502d90 at scope exit
        void** const Companion = Scoped.Slots;
        if (GetGameOption(kGameOptParticles) != 0 && Arch->VisEffect != nullptr &&
            Arch->VisEffect->Nickname[0] != '\0')
            Companion[2] = ParticleEffectCreate();
        if (Arch->SndEffect != nullptr)
            Companion[1] = CreateClientSoundSource3D();
        if (Arch->LgtRadius > 0.0f)
            Companion[3] = GameLightConstruct(static_cast<FGameLight*>(GameLightHeapAlloc(0x12c)));

        if (Companion[2] != nullptr)
            ParticleEffectAttach(static_cast<FParticleEffect*>(Companion[2]), Go, EngineIndex,
                                 reinterpret_cast<FTransform const*>(Pose));
        if (Companion[1] != nullptr)
            static_cast<ISoundSource*>(Companion[1])->SetPose(EngineIndex, Pose);
        if (Companion[3] != nullptr)
            CliExplosionAttachLocalXform(Companion[3], EngineIndex,
                                         reinterpret_cast<FTransform const*>(Pose));

        if (CreateEffectStart(Companion, Arch))
            CliEffectListInsert(&Go->EffectList, Companion);
    }

    // FUN_00545670 case 2 (0x005456ad..0x005459be): the bay doors' sound at the hull's
    // HpBaySurface hardpoint -- bay_doors_open_snd when the event's byte is set, else
    // bay_doors_close_snd. It is the createeffect.h sequence inlined on a stack EffectArch
    // that carries only the sound.
    void ShipBayDoorSound(FClientShipGameobj* Go, bool bOpen)
    {
        // The CShip narrowing (class & 0x503) is not null-tested before +0x88 is read.
        CObject* const Obj = Go->Object;
        CShip* const Ship = (Obj != nullptr &&
                             (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                                ? static_cast<CShip*>(Obj)
                                : nullptr;
        Archetype::Ship const* const ShipArch =
            static_cast<Archetype::Ship const*>(Ship->Archetype);

        // 0x005456de..0x005457ab: the hardpoint in the object's own frame; an empty name or a
        // miss leaves position zero and orientation identity (the CRC word is not written).
        HardpointInfo Hp;
        long HpHandle = 0;
        char const* const HpName = ShipArch->HpBaySurface.Value;
        if (HpName[0] == '\0' || !FindHardpoint_PS(Go->get_index(), HpName, HpHandle, Hp))
        {
            Hp.Position.x = 0.0f;
            Hp.Position.y = 0.0f;
            Hp.Position.z = 0.0f;
            Hp.Orient.E[0][0] = 1.0f;
            Hp.Orient.E[0][1] = 0.0f;
            Hp.Orient.E[0][2] = 0.0f;
            Hp.Orient.E[1][0] = 0.0f;
            Hp.Orient.E[1][1] = 1.0f;
            Hp.Orient.E[1][2] = 0.0f;
            Hp.Orient.E[2][0] = 0.0f;
            Hp.Orient.E[2][1] = 0.0f;
            Hp.Orient.E[2][2] = 1.0f;
        }

        // 0x005457b3..0x00545840: the stack EffectArch, its sound looked up (FUN_0042ae40) from
        // the CliShip companion's +0x58 / +0x5c id.
        FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
        FEffectArch& Arch = Scoped.Arch;
        ID_String const* const SoundId = Archetype::CliShipBayDoorSound(ShipArch->Companion, bOpen);
        Arch.SndEffect = LookupClientSoundArch(SoundId);

        // 0x00545847..0x0054587d: the pose handed to the effects -- the 3x3 then the position.
        float Pose[12];
        Pose[0] = Hp.Orient.E[0][0];
        Pose[1] = Hp.Orient.E[0][1];
        Pose[2] = Hp.Orient.E[0][2];
        Pose[3] = Hp.Orient.E[1][0];
        Pose[4] = Hp.Orient.E[1][1];
        Pose[5] = Hp.Orient.E[1][2];
        Pose[6] = Hp.Orient.E[2][0];
        Pose[7] = Hp.Orient.E[2][1];
        Pose[8] = Hp.Orient.E[2][2];
        Pose[9] = Hp.Position.x;
        Pose[10] = Hp.Position.y;
        Pose[11] = Hp.Position.z;
        long const EngineIndex = Go->get_index();

        // 0x00545883..0x005459a3; FUN_0040e2a0 (0x005459b5) is Scoped's destructor.
        CreateEffectOnGameobj(Go, &Arch, EngineIndex, Pose);
    }
}

void FClientShipGameobj::PlayCargoJettisonSound()
{
    // FUN_00543f50 (thiscall, bare RET) -- the bay-door sequence of FUN_00545670 case 2 with
    // SND_CARGO_JETTISONED as the sound.
    // 0x00543f6e..0x00543f7c: an empty SND_CARGO_JETTISONED (the Common export) ends here.
    if (SND_CARGO_JETTISONED.IsEmpty())
        return;

    // 0x00543f82..0x0054400b: the CShip (class & 0x503; `this` and +0xc tested, the result not)
    // archetype's +0xe0 HpBaySurface, found in the object's own frame (FindHardpoint_PS on
    // get_index); 0x0054400d..0x0054407d: an empty name or a miss leaves position zero and the
    // orientation identity.
    CObject* const Obj = Object;
    CShip* const Ship =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(Obj)
            : nullptr;
    Archetype::Ship const* const ShipArch = static_cast<Archetype::Ship const*>(Ship->Archetype);
    HardpointInfo Hp;
    long HpHandle = 0;
    char const* const HpName = ShipArch->HpBaySurface.Value;
    if (HpName[0] == '\0' || !FindHardpoint_PS(get_index(), HpName, HpHandle, Hp))
    {
        Hp.Position.x = 0.0f;
        Hp.Position.y = 0.0f;
        Hp.Position.z = 0.0f;
        Hp.Orient = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    // 0x00544085..0x005440a4: the stack EffectArch (FUN_0040e250) with its sound looked up
    // (FUN_0042ae40) from SND_CARGO_JETTISONED.
    FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
    FEffectArch& Arch = Scoped.Arch;
    Arch.SndEffect = LookupClientSoundArch(&SND_CARGO_JETTISONED);

    // 0x005440a4..0x005440dc: the pose -- the 3x3, then the position.
    float Pose[12];
    Pose[0] = Hp.Orient.E[0][0];
    Pose[1] = Hp.Orient.E[0][1];
    Pose[2] = Hp.Orient.E[0][2];
    Pose[3] = Hp.Orient.E[1][0];
    Pose[4] = Hp.Orient.E[1][1];
    Pose[5] = Hp.Orient.E[1][2];
    Pose[6] = Hp.Orient.E[2][0];
    Pose[7] = Hp.Orient.E[2][1];
    Pose[8] = Hp.Orient.E[2][2];
    Pose[9] = Hp.Position.x;
    Pose[10] = Hp.Position.y;
    Pose[11] = Hp.Position.z;
    // 0x005440e3: get_index.
    long const EngineIndex = get_index();

    // 0x005440e6..0x005441fa: the createeffect.h sequence (EFFECTS gate, factory, attach,
    // FUN_00544230, FUN_004fcea0, FUN_00502d90); 0x00544211: FUN_0040e2a0.
    CreateEffectOnGameobj(this, &Arch, EngineIndex, Pose);
}

// The three NN voice lines the ship notify queues (static-init CreateID entries).
unsigned G_ShipVoiceCargoExpiredId = CreateID("cargo_expired");     // DAT_00678b58, LAB_00542910
unsigned G_ShipVoiceWeaponExpiredId = CreateID("weapon_expired");   // DAT_00678b54, LAB_00542930
unsigned G_ShipVoiceEngineCriticalId = CreateID("engine_critical"); // DAT_00678b4c, LAB_00542970
// DAT_00678b50, LAB_00542950 -- queued by slot 102 (LAB_00545f50, 0x0054602b) below 20% hull.
unsigned G_ShipVoiceHullBreachId = CreateID("hull_breach_imminent");
// The request-answer and IObjAffect slot 27 voice lines (static-init CreateID entries; readers
// FUN_00546d00 and FUN_00547250).
unsigned G_ShipVoiceTradeLaneDisruptedId = CreateID("nnvoice_trade_lane_disrupted"); // DAT_00678b34, LAB_00542a30
unsigned G_ShipVoiceDockGrantedId = CreateID("dock_granted");               // DAT_00678b30, LAB_00542a50
unsigned G_ShipVoiceDockDeniedHostileId = CreateID("dock_denied_hostile");  // DAT_00678b2c, LAB_00542a70
unsigned G_ShipVoiceDockDeniedLockedId = CreateID("dock_denied_locked");    // DAT_00678b28, LAB_00542a90
unsigned G_ShipVoiceCannotDockId = CreateID("cannot_dock");                 // DAT_00678b24, LAB_00542ab0
unsigned G_ShipVoiceWaitingToDockId = CreateID("waiting_to_dock");          // DAT_00678b20, LAB_00542ad0
unsigned G_ShipVoiceDockTooFarId = CreateID("nnv_dock_too_far");            // DAT_00678b1c, LAB_00542af0
unsigned G_ShipVoiceFormationLostId = CreateID("nnv_formation_lost");       // DAT_00678b10, LAB_00542b50

void FClientShipGameobj::notify(Event Code, void* Param)
{
    // FUN_00545670 (INotify slot 0 on the +0xac sub-object, RET 8): switch on Code - 2 (JA
    // above 7 -> straight to the tail), then always EquippedObj's notify (0x00545a4d).
    switch (static_cast<int>(Code))
    {
    case 2:
        ShipBayDoorSound(this, *static_cast<unsigned char const*>(Param) != 0);
        break;
    case 3:
        // 0x005459d7..0x00545a33: the player's ship only -- the voice line "weapon_expired"
        // (DAT_00678b54) through FUN_00489e80(id, 0), the NN voice queue.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            NnVoiceEnqueue(G_ShipVoiceWeaponExpiredId, 0);
        break;
    case 4:
        // 0x005459c0..0x00545a33: the same with "cargo_expired" (DAT_00678b58).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            NnVoiceEnqueue(G_ShipVoiceCargoExpiredId, 0);
        break;
    case 9:
    {
        // 0x005459ee..0x00545a33: the player's ship, a CArchGroup whose collision group type
        // has a bit of 0x3f00 and whose relative health is below 0.05 (DAT_005d5b58; FCOMP
        // double / TEST AH,0x5 / JP: only C0 alone goes on, so equal and NaN stop) --
        // "engine_critical" (DAT_00678b4c).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
            break;
        CArchGroup* const Group = static_cast<CArchGroup*>(Param);
        if (Group == nullptr)
            break;
        if ((Group->GetCollisionGroupType() & 0x3f00u) == 0u)
            break;
        if (!(static_cast<double>(Group->GetRelativeHealth()) < 0.05))
            break;
        NnVoiceEnqueue(G_ShipVoiceEngineCriticalId, 0);
        break;
    }
    default:
        break;
    }
    FClientEquippedObj::notify(Code, Param);
}

namespace
{
    // FUN_005478f0 codes 0 and 1: the solar's open_sound (CliSolar +0x54) or close_sound
    // (+0x58) at the root, pose identity -- the same createeffect.h sequence as the ship's bay
    // doors, with FUN_005480c0 doing the attach.
    void SolarDoorSound(FClientSolarGameobj* Go, bool bOpen)
    {
        // The archetype is read through the 0x303 narrowing without a null test.
        CObject* const Obj = Go->Object;
        CObject* const Solar = (Obj != nullptr &&
                                (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                                   ? Obj
                                   : nullptr;
        FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
        FEffectArch& Arch = Scoped.Arch;
        ID_String const* const SoundId =
            Archetype::CliSolarDoorSound(Solar->Archetype->Companion, bOpen);
        Arch.SndEffect = LookupClientSoundArch(SoundId);

        float Pose[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        long const EngineIndex = Go->get_index();
        CreateEffectOnGameobj(Go, &Arch, EngineIndex, Pose);
    }
}

void FClientSolarGameobj::notify(Event Code, void* Param)
{
    // FUN_005478f0 (INotify slot 0 on the +0xac sub-object, RET 8). Codes 0, 1 and 0x11; every
    // path ends in EquippedObj's notify (0x00548092).
    int const Kind = static_cast<int>(Code);
    if (Kind == 0)
    {
        // 0x00547e3c..0x00548077: when the solar archetype's +0x81 byte is set, the open or
        // close sound by the event's byte.
        CObject* const Obj = Object;
        CObject* const Solar = (Obj != nullptr &&
                                (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                                   ? Obj
                                   : nullptr;
        Archetype::EqObj const* const SolarArch =
            static_cast<Archetype::EqObj const*>(Solar->Archetype);
        if (SolarArch->bDockingCamera)
            SolarDoorSound(this, *static_cast<unsigned char const*>(Param) != 0);
    }
    else if (Kind == 1)
    {
        // 0x0054798c: a sub-code in the first dword of Param -- 0, 2 and 3 (1 does nothing).
        // The "has a jump gate effect" test is TypeBits (slot 111) & 0x40, else & 0x800 -- two
        // calls, as retail makes them.
        int const Sub = *static_cast<int const*>(Param);
        if (Sub == 0)
        {
            // 0x00547beb..0x00547e37: open sound, then, if there is none, the JumpGateEffect at
            // +0x204 -- operator new(0xe0) and FUN_00502ba0(this) (0x00547e1f); a failed
            // allocation stores null.
            SolarDoorSound(this, true);
            if ((Slot1bc() & 0x40u) != 0u || (Slot1bc() & 0x800u) != 0u)
            {
                if (Field_0x204 == nullptr)
                    Field_0x204 = new (std::nothrow) FJumpGateEffect(this);
            }
        }
        else if (Sub == 2)
        {
            // 0x005479f7..0x00547bdf: close sound, then FUN_00503380 on the effect, when there is
            // one (0x00547bda: its +0x18 fire flag = 1).
            SolarDoorSound(this, false);
            if ((Slot1bc() & 0x40u) != 0u || (Slot1bc() & 0x800u) != 0u)
            {
                if (Field_0x204 != nullptr)
                    JumpGateEffectFire(static_cast<FJumpGateEffect*>(Field_0x204));
            }
        }
        else if (Sub == 3)
        {
            // 0x005479a3..0x005479f2: destroy the effect, when there is one (FUN_00502e40 at
            // 0x005479da + operator delete), and clear +0x204 either way.
            if ((Slot1bc() & 0x40u) != 0u || (Slot1bc() & 0x800u) != 0u)
            {
                if (Field_0x204 != nullptr)
                    delete static_cast<FJumpGateEffect*>(Field_0x204);
                Field_0x204 = nullptr;
            }
        }
    }
    else if (Kind == 0x11)
    {
        // 0x00547937..0x00547987: a solar with TypeBits bit 7 -- FUN_00516910(this, event byte)
        // switches its trade lane ring and the result lands in +0x1f0 (0x00547981).
        CObject* const Obj = Object;
        CSimple const* const Simple =
            (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                ? static_cast<CSimple const*>(Obj)
                : nullptr;
        if ((Simple->TypeBits & 0x80u) != 0u)
            Field_0x1f0 =
                TradeLaneSolarLaneSwitch(this, *static_cast<unsigned char const*>(Param));
    }
    FClientEquippedObj::notify(Code, Param);
}

void FClientShipGameobj::create(Archetype::Root* Arch)
{
    // LAB_00546550.
    CreateOpenConnect(this, CObject::ClassShip, Arch);
}

bool FClientShipGameobj::find(unsigned const& Id)
{
    // FUN_005465d0.
    return FindAndConnect(this, CObject::ClassShip, Id);
}

void FClientShipGameobj::Slot170()
{
    // FUN_005447b0 (RET 0).
    FClientEquippedObj::Slot170();                                             // 0x005447b4
    Field_0x18c = static_cast<unsigned char>(Field_0x18c & 0xfcu);             // 0x005447c6
    // 0x005447c2..0x005447cc: the jump state back to 0 (FUN_005437d0).
    SetJumpState(0);

    // 0x005447d1..0x00544801: the pilot mesh named by the ship companion's pilot_mesh (+0x64),
    // through the CShip's archetype (+0x88) and its companion (+0x04). The CShip is narrowed
    // (0x503) with no null test of the result.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    AttachPilotMesh(reinterpret_cast<ID_String const*>(
        reinterpret_cast<unsigned char const*>(Ship->Archetype->Companion) + 0x64));

    // 0x00544806..0x00544846: the local player's ship (the CShip's +0xb4 against DAT_00673344)
    // gets its behaviour interface for the "Player" state graph.
    if (Ship->OwnerPlayer == GetPlayerConnectionId())
    {
        int const GraphId = pub::StateGraph::get_state_graph("Player", pub::StateGraph::LEADER);
        Ship->create_behavior_interface(static_cast<IObjRW*>(this), GraphId);
    }

    // 0x0054484c..0x00544868: collisions back on unless the ship is launching or jumping.
    if (!Ship->is_launching() && !Ship->is_jumping())
        Ship->enable_collisions_r(true);
}

void FClientShipGameobj::AttachPilotMesh(ID_String const* PilotMesh)
{
    // FUN_00544380 (RET 4). 0x00544392..0x0054439c: an empty name does nothing.
    if (PilotMesh->IsEmpty())
        return;

    // 0x005443a2..0x005443c6: "HpPilot" on the ship's own instance (vt+0x1c get_index); no
    // hardpoint, nothing.
    long Handle = 0;
    HardpointInfo PilotInfo;
    if (!FindHardpoint_PS(get_index(), "HpPilot", Handle, PilotInfo))
        return;

    // 0x005443cc..0x005443e0: the simple archetype PilotMesh names, kept at +0x104.
    Field_0x104 = Archetype::GetSimple(PilotMesh->Id);
    if (Field_0x104 == nullptr)
        return;
    // 0x005443e6..0x005443fa: its load (vt+0xc), then its companion's Load(0) (vt+0x20).
    Field_0x104->load();
    Field_0x104->Companion->Load(0);
    // 0x005443fd..0x00544420: an engine instance of it (no parent); -1 stops here, anything
    // else goes to +0x10c (the mesh holder's instance).
    long const Instance = Field_0x104->create_instance(nullptr);
    if (Instance == -1)
        return;
    Field_0x108.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(Instance));

    // 0x00544410..0x00544426: "HpConnect" on the pilot mesh, into the same handle; the result
    // is not tested. Retail reads its stack HardpointInfo whether or not the lookup filled it;
    // the port starts it zeroed so a miss reads zeros instead of stale stack.
    HardpointInfo ConnectInfo = {};
    FindHardpoint_PS(Instance, "HpConnect", Handle, ConnectInfo);

    // 0x00544428..0x00544488: +0x118 = {HpPilot's orientation, HpPilot's position minus
    // HpConnect's position} (twelve dwords, MOVSD.REP). Each difference is an x87 FSUB stored
    // to a float.
    Field_0x118.Rotation = PilotInfo.Orient;
    Field_0x118.Translation.x = PilotInfo.Position.x - ConnectInfo.Position.x;
    Field_0x118.Translation.y = PilotInfo.Position.y - ConnectInfo.Position.y;
    Field_0x118.Translation.z = PilotInfo.Position.z - ConnectInfo.Position.z;
}

IObjRW* FClientShipGameobj::PickNextTarget(int ConnectionId)
{
    // FUN_005449f0 (RET 4). ConnectionId is pushed by the caller and never read.
    (void)ConnectionId;
    // 0x005449fa..0x00544a0c: two (distance, object) bests, the distances at 1.0e9f
    // (0x4e6e6b28): all candidates and the watch-listed ones.
    IObjRW* Nearest = nullptr;
    IObjRW* NearestListed = nullptr;
    float NearestDist = 1.0e9f;
    float NearestListedDist = 1.0e9f;

    // 0x00544a14..0x00544a37: this object through cobject() (vt+0x150), narrowed to a CShip
    // ((class & 0x503) == 0x503) or null; no null test before the equip manager (+0xe4).
    CObject* Obj = const_cast<CObject*>(cobject());
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
        Obj = nullptr;
    CShip* const Ship = static_cast<CShip*>(Obj);

    // 0x00544a3b..0x00544a66: the scanner (type 0x80000); none, or an empty list -> Nearest (null).
    CEScanner* const Scanner = static_cast<CEScanner*>(Ship->EquipManager.FindFirst(0x80000u));
    if (Scanner == nullptr)
        return Nearest;

    for (int I = 0; I < Scanner->Scans.Count; ++I)
    {
        // 0x00544a70..0x00544a81: the watched Watchable, back to its IObjRW (minus 8).
        IObjRW* const Other =
            static_cast<IObjRW*>(const_cast<Watchable*>(Scanner->Scans.Entries[I].target()));
        if (Other == nullptr)
            continue;
        // 0x00544a87..0x00544aa5: CShip::is_enemy, then the object's is_dying (vt+0x38).
        bool bListed = false;
        if (!Ship->is_enemy(Other))
            continue;
        if (Other->is_dying())
            continue;
        // 0x00544aab..0x00544aef: a CSimple ((class & 3) == 3) counts only with TypeBits &
        // 0x7f0000 or when its gameobj (FUN_005416c0(get_id, 0)) is watch-listed (+0xa8).
        CObject const* const OtherObj = Other->cobject();
        if (OtherObj != nullptr && (static_cast<unsigned>(OtherObj->ObjectClass) & 3u) == 3u)
        {
            FClientGameobj* const OtherGo = ClientObjDb_FindObject(Other->get_id(), 0);
            if (OtherGo != nullptr)
                bListed = OtherGo->bWatchListed != 0;
            if ((static_cast<CSimple const*>(OtherObj)->TypeBits & 0x7f0000u) == 0 && !bListed)
                continue;
        }
        // 0x00544af1..0x00544b26: distance from this object's position (vt+0) to the other's.
        Vector const& Mine = get_position();
        Vector const& Theirs = Other->get_position();
        // x87 retains the differences and distance until the best-distance FST dword.
        double const Dx = static_cast<double>(Theirs.x) - Mine.x;
        double const Dy = static_cast<double>(Theirs.y) - Mine.y;
        double const Dz = static_cast<double>(Theirs.z) - Mine.z;
        double const Dist = std::sqrt(Dx * Dx + Dz * Dz + Dy * Dy);
        // 0x00544b28..0x00544b52: TEST AH,0x5 + JP skips greater/equal/unordered;
        // less falls through and stores the new best distance as float.
        if (Dist < NearestDist)
        {
            NearestDist = static_cast<float>(Dist);
            Nearest = Other;
        }
        if (bListed && Dist < NearestListedDist)
        {
            NearestListedDist = static_cast<float>(Dist);
            NearestListed = Other;
        }
    }

    // 0x00544b71..0x00544b7f: the watch-listed best when there is one.
    if (NearestListed != nullptr)
        return NearestListed;
    return Nearest;
}

void FClientShipGameobj::SetTargetWithGuns(IObjRW* Target, unsigned short SubTarget)
{
    // FUN_00544b90 (RET 8). 0x00544ba1..0x00544bb2: IObjAffect vt+0x48 on this object's
    // IObjAffect face with the player's connection id.
    static_cast<IObjAffect*>(this)->set_target(Target, SubTarget,
                                               static_cast<int>(GetPlayerConnectionId()));

    // 0x00544bb5..0x00544c18: every gun (CEquipTraverser(0x80)); the CShip is narrowed again on
    // each pass and its equip manager (+0xe4) used with no null test. Each gun's id word (+8)
    // goes to FUN_005351d0 -- FClientEquippedObj::slot_44, a direct (non-virtual) call.
    CEquipTraverser Walk(0x80);
    for (;;)
    {
        CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
        CEquip* const Gun = Ship->EquipManager.Traverse(Walk);
        if (Gun == nullptr)
            break;
        unsigned short const GunId = Gun->Id;
        FClientEquippedObj::slot_44(&GunId, Target, SubTarget,
                                    static_cast<int>(GetPlayerConnectionId()));
    }
}

bool FClientShipGameobj::RequestTractor(IObjRW* Target)
{
    // FUN_00544c30 (RET 4). 0x00544c30..0x00544c6e: the CShip's tractor (type 0x100000); none
    // -> false.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    CEquip* const Tractor = Ship->EquipManager.FindFirst(0x100000u);
    if (Tractor == nullptr)
        return false;

    unsigned short const TractorId = Tractor->Id;
    IObjAffect* const Affect = static_cast<IObjAffect*>(this);
    // 0x00544c75..0x00544cc4: a Target and a tractor that is not destroyed (CEquip vt+4): a
    // CLoot target ((class & 0x803) == 0x803) gets vt+0x40 (id, Target, connection); any
    // other target nothing.
    if (Target != nullptr && !Tractor->IsDestroyed())
    {
        CObject const* const TargetObj = Target->cobject();
        if (TargetObj != nullptr &&
            (static_cast<unsigned>(TargetObj->ObjectClass) & 0x803u) == 0x803u)
        {
            Affect->slot_40(&TractorId, Target, static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }
    // 0x00544ccf..0x00544cec: otherwise vt+0x4c (id, false, connection).
    Affect->slot_4c(&TractorId, false, static_cast<int>(GetPlayerConnectionId()));
    return true;
}

// DAT_00613f58 / DAT_00613f5c / DAT_00613f60 (.data, read only by the ship update): the
// "fade the tunnel out" latch (initially set) and the two fade times.
unsigned char G_bJumpTunnelFadeOutPending = 1; // DAT_00613f58
float G_JumpTunnelFadeOutTime = 1.5f;          // DAT_00613f5c
float G_JumpTunnelFadeInTime = 1.5f;           // DAT_00613f60

// The three NN voice lines of the atmosphere / corona warning (static-init CreateID entries).
unsigned G_ShipVoiceDangerId = CreateID("danger");                           // DAT_00678b48, LAB_00542990
unsigned G_ShipVoiceEnteringSunCoronaId = CreateID("entering_sun_corona");   // DAT_00678b44, LAB_005429b0
unsigned G_ShipVoiceEnteringAtmosphereId = CreateID("entering_atmosphere");  // DAT_00678b40, LAB_005429d0

void FClientShipGameobj::update(float Dt)
{
    // FUN_00545050 (RET 4). 0x00545056..0x00545060: nothing for a gameobj slot 86 marks as
    // reaped.
    if (Slot158())
        return;

    // 0x00545066..0x0054508e: the CShip (0x503), kept for the whole function with no null
    // test of it anywhere below.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));

    // 0x00545092..0x005450e9: the player's ship follows the DISABLE_COLLIDE option (0x37):
    // the value is latched on first use (DAT_00678b08, guard bit 0 of DAT_00678b0c) and, when
    // the option changes, collisions go on for 0 and off otherwise.
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        static int SDisableCollide = GetGameOption(kGameOptDisableCollide);
        if (SDisableCollide != GetGameOption(kGameOptDisableCollide))
        {
            SDisableCollide = GetGameOption(kGameOptDisableCollide);
            Ship->enable_collisions_r(SDisableCollide == 0);
        }
    }

    // 0x005450ef..0x0054514d: the object's owner (class & 3 -> +0xb4, else DAT_00673344).
    // The local player's objects run the full CObject::update; others, in multiplayer only,
    // the dumb one; in single player a foreign object is not updated here at all.
    CObject* const OwnerObject = NarrowObject(this, 0x3u);
    unsigned const Owner = (OwnerObject != nullptr)
                               ? static_cast<CSimple const*>(OwnerObject)->OwnerPlayer
                               : GetPlayerConnectionId();
    if (Owner == GetPlayerConnectionId())
        Ship->update(Dt, CObject::UPDATE_DEFAULT);
    else if (!SinglePlayer())
        Ship->update(Dt, CObject::UPDATE_DUMB);

    // 0x00545150..0x00545157: EquippedObj's update, called directly.
    FClientEquippedObj::update(Dt);

    // 0x0054515c..0x005451d9: the +0xe4 countdown. Only while it is non-zero (TEST AH,0x44 /
    // JNP: equal skips, NaN goes on) it runs down by Dt (FST keeps the x87 value for the
    // compare); at or below 0 (TEST AH,0x41 / JP: greater and NaN skip) close_bay runs +0xe8
    // times, the CShip narrowed again each time, and +0xe4 returns to 0.
    if (Field_0xe4 != 0.0f)
    {
        double const Remaining = static_cast<double>(Field_0xe4) - static_cast<double>(Dt);
        Field_0xe4 = static_cast<float>(Remaining);
        if (Remaining <= 0.0)
        {
            while (Field_0xe8 > 0)
            {
                static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->close_bay();
                Field_0xe8 = Field_0xe8 - 1;
            }
            Field_0xe4 = 0.0f;
        }
    }

    // 0x005451d9..0x00545374: the jump, while the state is non-zero and there is an effect.
    if (Field_0x158 != 0 && Field_0x188 != nullptr)
    {
        FJumpShipEffect* const JumpFx = static_cast<FJumpShipEffect*>(Field_0x188);
        // 0x005451f5..0x0054521d: the step is Dt, or 0.1f (0x3dcccccd) when Dt is above
        // DAT_005d3ee0 (0.1f; TEST AH,0x41 / JNZ keeps Dt for not-above and NaN); the effect's
        // clock advances by it.
        float JumpDt = Dt;
        if (Dt > 0.1f)
            JumpDt = 0.1f;
        JumpFx->Field_0x70 = JumpFx->Field_0x70 + JumpDt;

        // 0x00545220..0x0054527b: in the tunnel (state 2), once, when no more than 1.5 s of
        // JumpOutTunnelTime remain (TEST AH,0x41 / JP: greater and NaN skip), the screen fades
        // out to white (FUN_004122a0).
        if (G_bJumpTunnelFadeOutPending != 0 && Field_0x158 == 2 &&
            static_cast<double>(JumpFx->JumpOutTunnelTime) -
                    static_cast<double>(JumpFx->Field_0x70) <=
                static_cast<double>(G_JumpTunnelFadeOutTime))
        {
            G_bJumpTunnelFadeOutPending = 0;
            float const White[3] = {1.0f, 1.0f, 1.0f};
            FadeOutColoured(White, G_JumpTunnelFadeOutTime);
        }

        // 0x0054527e..0x0054535b: each state moves on once its time is below the clock
        // (FCOMP + TEST AH,0x5 / JP: only "less" goes on).
        switch (Field_0x158)
        {
        case 1:
            if (JumpFx->JumpOutTime < JumpFx->Field_0x70)
                SetJumpState(2);
            break;
        case 2:
            // 0x005452ca..0x005452ff: the fade latch re-armed, the switch requested, and the
            // screen faded back in from white (FUN_004121d0, starting opaque).
            if (JumpFx->JumpOutTunnelTime < JumpFx->Field_0x70)
            {
                G_bJumpTunnelFadeOutPending = 1;
                SetJumpState(3);
                float const White[3] = {1.0f, 1.0f, 1.0f};
                FadeInColoured(White, G_JumpTunnelFadeInTime, true);
            }
            break;
        case 3:
            // 0x0054531c..0x00545324: only once the jump in was ordered (+0x15c).
            if (JumpFx->JumpInTunnelTime < JumpFx->Field_0x70 && Field_0x15c != 0)
                SetJumpState(4);
            break;
        case 4:
            if (JumpFx->JumpInTime < JumpFx->Field_0x70)
                SetJumpState(5);
            break;
        case 5:
            // 0x00545341..0x00545357: done once the clock is above 1.0f (TEST AH,0x41 / JNZ).
            if (JumpFx->Field_0x70 > 1.0f)
                SetJumpState(0);
            break;
        default:
            break;
        }

        // 0x00545360..0x0054536f: the effect, when it survived the switch (state 0 frees it),
        // gets the same clamped step (FUN_00503cd0).
        if (Field_0x188 != nullptr)
            static_cast<FJumpShipEffect*>(Field_0x188)->Update(JumpDt);
    }

    // 0x00545374..0x0054537e: PhySys's collision state of the object (+0x10, not narrowed).
    bool const bCollides = PhySys::GetCollisionState(Object);
    // The CSolar the ship is inside, which CShip::update keeps at +0x218, and its type bits
    // (+0xe0: 2 planet, 4 sun).
    CSolar* const Atmosphere = Ship->AtmosphereSolar;

    // 0x00545386..0x005453c2: the burn companion at +0xf4 is alive when its particle is not
    // finished (+8 bit 1 clear), its sound is active or its light's +0xd4 is 0 -- the
    // FUN_00503330 test, inlined here.
    if (CreateEffectCompanionAlive(Field_0xf4) != 0)
    {
        // 0x005454f6..0x00545511: it leaves the effect list (FUN_004fcef0) once the ship stops
        // colliding, leaves the atmosphere or is landing (+0x18c bit 3).
        if (!bCollides || Atmosphere == nullptr || (Field_0x18c & 0x8u) != 0)
            CliEffectListRemove(reinterpret_cast<unsigned char*>(this) + 0x28, Field_0xf4);
    }
    else if (bCollides && Atmosphere != nullptr && (Field_0x18c & 0x8u) == 0)
    {
        // 0x005453eb..0x0054548f: "atmosphere_burn" started on the ship's own instance with an
        // identity transform, through DAT_00674c60, into the companion and the effect list.
        ID_String BurnEffect;
        BurnEffect.Id = CreateID("atmosphere_burn");
        FTransform Local;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Local.Rotation.D[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Local.Translation.X = 0.0f;
        Local.Translation.Y = 0.0f;
        Local.Translation.Z = 0.0f;
        int const Index = static_cast<int>(get_index());
        if (!BurnEffect.IsEmpty())
            CliCreateEffectOneShot(FindEffectArch(&BurnEffect.Id), this, Index, &Local,
                                   Field_0xf4);

        // 0x00545494..0x005454f4: the player hears "danger" and "entering_sun_corona" in a sun
        // (bit 2), else "danger" and "entering_atmosphere" in a planet (bit 1), through the NN
        // voice queue (FUN_00489e80(id, 0)).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            unsigned const TypeBits = Atmosphere->TypeBits;
            if ((TypeBits & 0x4u) != 0)
            {
                NnVoiceEnqueue(G_ShipVoiceDangerId, 0);
                NnVoiceEnqueue(G_ShipVoiceEnteringSunCoronaId, 0);
            }
            else if ((TypeBits & 0x2u) != 0)
            {
                NnVoiceEnqueue(G_ShipVoiceDangerId, 0);
                NnVoiceEnqueue(G_ShipVoiceEnteringAtmosphereId, 0);
            }
        }
    }

    // 0x00545516..0x00545567: the thruster sound at +0xf0 -- created (FUN_00428f80(0x50) +
    // FUN_0052efb0(this), class CliThrusterSound, vtable 005ddbfc) the first time the CShip
    // carries a thruster (CEquipManager::FindFirst(0x400)), a failed allocation storing null;
    // then, while set, ticked (FUN_0052f110(Dt)) every update. Both live in menu_sound_arch.cpp.
    if (Field_0xf0 == nullptr && Ship->EquipManager.FindFirst(EquipTypeThruster) != nullptr)
        Field_0xf0 = CreateClientThrusterSound(this);
    if (Field_0xf0 != nullptr)
        TickClientThrusterSound(Field_0xf0, Dt);

    // 0x0054556c..0x00545589: +0x150 runs down by Dt while above 0 (TEST AH,0x41 / JNZ).
    if (Field_0x150 > 0.0f)
        Field_0x150 = Field_0x150 - Dt;
}

void FClientShipGameobj::Slot184(void* Camera)
{
    // LAB_00546480 (RET 4). 0x00546486..0x005464a1: only the player's ship in jump states 2
    // or 3 (the tunnel) lights itself; everything else is GameObject's slot 97 (FUN_00539b40).
    if ((Field_0x158 == 2 || Field_0x158 == 3) && this == GetPlayerObjRW())
    {
        // 0x005464a7: FUN_004101e0 -- every registered light off and unlinked.
        EngineLightClearRegisteredList();
        // 0x005464ac..0x005464b4: no jump effect -> done.
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Field_0x188);
        if (Effect == nullptr)
            return;
        // 0x005464ba..0x005464c9: its jump_ambient bytes (FUN_00504870 = [+0x1c]), when set,
        // into D3DRS_AMBIENT (FUN_00410240).
        if (Effect->Ambient != nullptr)
            EngineLightSetAmbientRgbBytes(Effect->Ambient);
        // 0x005464cc..0x005464d7: no tunnel (+0x24) -> done.
        if (Effect->Tunnel == nullptr)
            return;
        // 0x005464d9..0x005464f5: the ship's position (vt+0) copied into a local.
        Vector Pos = get_position();
        // 0x005464f9..0x00546508: the tunnel's light (+0x2a8); none -> done.
        FGameLight* const Light = Effect->Tunnel->Light;
        if (Light == nullptr)
            return;
        // 0x0054650a..0x0054650f: FUN_00410ef0 on the light -- its position is the ship's.
        EngineLightSetPosition((FEngineLight*)Light, &Pos.x);
        // 0x00546514..0x00546525: FUN_004102c0(&light, 1, Camera, &Pos).
        FEngineLight* const Lights[1] = {(FEngineLight*)Light};
        EngineLightApplyList(Lights, 1u, static_cast<ICamera*>(Camera), &Pos.x);
        return;
    }
    FClientGameobj::Slot184(Camera);
}

namespace
{
    // FUN_005462d0 (cdecl) -- DALib::Engine QI for IFLAppearance into a zeroed local (a smart
    // pointer: EH frame LAB_005c0f38); QI < 0 releases a non-null local and returns false;
    // otherwise Slot4 (+0x10) (Id, Out), true when it returned >= 0, then the release.
    bool GetAppearanceInstanceValue(int Id, void** Out)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->GetInstanceValue(Id, Out) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

    // FUN_00546380 (cdecl) -- the same QI; on success Slot6 (+0x18) (First, Second); the local
    // is released whenever it is non-null. No result.
    void SetAppearanceDirectionalLight(float const* First, float const* Second)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
            Ifl->SetDirectionalLight(First, Second);
        if (Ifl != nullptr)
            Ifl->Release();
    }

    // FUN_00546400 (cdecl) -- the same around Slot7 (+0x1c) (Flag).
    void SetAppearanceDirectionalLightEnabled(unsigned char Flag)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
            Ifl->SetDirectionalLightEnabled(Flag);
        if (Ifl != nullptr)
            Ifl->Release();
    }
}

unsigned* FClientShipGameobj::Slot188(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                      void* Matrix, void* Extra)
{
    // LAB_00546050 (RET 0x14) -- the ship draw.
    // 0x00546059..0x00546084: from the cockpit camera (FUN_0054bc80) the player's own ship is
    // not drawn: *OutLod = {0, 0xffff}.
    if (IsFirstPersonCameraCurrent() && GetPlayerObjRW() == this)
    {
        *OutLod = 0xffff0000u;
        return OutLod;
    }

    // 0x00546087..0x005460dc: the pilot mesh -- option 0x1d on (FUN_005792b0 > 0), the cached
    // LOD's low word at least 3 (unsigned, JC skips), its high word below 2 and below 1 (two
    // signed JGE tests) and a bound pilot instance (FUN_00420d60 on +0x108).
    unsigned short const LodLow = static_cast<unsigned short>(CachedLod & 0xffffu);
    short const LodHigh = static_cast<short>(CachedLod >> 16);
    if (GetGameOption(0x1d) > 0 && LodLow >= 3 && LodHigh < 2 && LodHigh < 1 &&
        ClientMeshHolderHasInstance(&Field_0x108))
    {
        // 0x005460e2..0x00546118: the CEqObj's +0x140 fade (the object narrowed by 0x103, no
        // test) strictly above 0 (FCOMP 0.0 / TEST AH,0x41 / JNZ skips not-greater): the
        // appearance Slot8(3, &Extra) (FUN_00533930), Slot4(get_index, &Value) (FUN_005462d0)
        // and Slot3(pilot instance, Value) (FUN_00533880).
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, 0x103u));
        if (EqObj->CloakPercent > 0.0f)
        {
            GetAppearanceOverrideMaterial(3u, &Extra);
            // Retail leaves this local unwritten when the QI fails; zeroed here.
            void* Value = nullptr;
            GetAppearanceInstanceValue(static_cast<int>(get_index()), &Value);
            SetAppearanceInstanceValue(Field_0x108.EngineInstance, Value);
        }

        // 0x00546152..0x00546158: vt+0x184 (Slot184) with the camera.
        Slot184(Camera);
        // 0x0054615e..0x0054618a: the pilot's world transform -- vt+0x10 (the object's
        // transform) times +0x118 through ship.cpp's 3DMathEngine (DAT_00678b68) slot 7
        // MulTransform into a local.
        FTransform const& ShipXform = reinterpret_cast<FTransform const&>(get_orientation());
        FTransform PilotXform;
        GetShipMathEngine()->MulTransform(PilotXform, ShipXform,
                                          reinterpret_cast<FTransform const&>(Field_0x118));
        // 0x0054618d..0x005461a2: copied (FUN_00406880) and set on the pilot instance
        // (FUN_00421050).
        FTransform Copy;
        CopyTransform12(&Copy, &PilotXform);
        ClientMeshHolderSetMatrix(&Field_0x108, &Copy);
        // 0x005461a7..0x005461cb: DALib::Renderer slot 7 (Camera, pilot instance, 1.0f, 8, 0,
        // Extra).
        DALib::Renderer->RenderInstance(Camera, Field_0x108.EngineInstance, 1.0f, 8u, nullptr, Extra);
    }

    // 0x005461cf..0x005461d6: FUN_00546400(0) -- the appearance Slot7(0).
    SetAppearanceDirectionalLightEnabled(0);

    // 0x005461d9..0x0054627b: with a star (FUN_0050c620's list non-empty, its first node's
    // sphere set), the unit direction from the star (FUN_005125f0) to this ship's position
    // (vt+0), computed on the FPU (FSQRT, FDIVR 1.0 stored as a float), goes to the appearance
    // Slot6(&direction, &{1, 1, 1}) (FUN_00546380).
    FStarSphereList* const Stars = StarSphereList();
    if (Stars != nullptr && Stars->Count != 0 && Stars->Sentinel->Next->Sphere != nullptr)
    {
        Vector StarPos;
        StarSphereGetOwnerPosition(Stars->Sentinel->Next->Sphere, &StarPos);
        Vector const& Pos = get_position();
        double const Dz = static_cast<double>(Pos.z) - StarPos.z;
        double const Dy = static_cast<double>(Pos.y) - StarPos.y;
        double const Dx = static_cast<double>(Pos.x) - StarPos.x;
        float const White[3] = {1.0f, 1.0f, 1.0f};
        float const Inv = static_cast<float>(1.0 / std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        float const Dir[3] = {static_cast<float>(Dx * Inv), static_cast<float>(Dy * Inv),
                              static_cast<float>(Dz * Inv)};
        SetAppearanceDirectionalLight(Dir, White);
    }

    // 0x00546283..0x005462b5: EquippedObj's draw FUN_00533780 into a local, copied to *OutLod.
    unsigned Lod = 0;
    unsigned* const Result =
        EquippedObjSubmitDrawRetail(this, &Lod, Camera, CachedLod, Matrix, Extra);
    *OutLod = *Result;
    return OutLod;
}

void FClientShipGameobj::Slot198(DamageList const& Damage)
{
    // LAB_00545f50 (RET 4). 0x00545f52..0x00545fac: damage of cause 6 on the player's ship ->
    // IObjAffect (+4) vt+0x50 slot_50(false, true, DAT_00673344), then the ship's behavior
    // manager (CEqObj::get_behavior_interface on the object narrowed by 0x503, no test of the
    // narrowing), when there is one, IBehaviorManager::update_current_behavior_cruise(false).
    if (static_cast<int>(Damage.Cause) == 6 && this == GetPlayerObjRW())
    {
        static_cast<IObjAffect*>(this)->slot_50(false, true,
                                                static_cast<int>(GetPlayerConnectionId()));
        IBehaviorManager* const Behavior =
            static_cast<CEqObj*>(NarrowObject(this, 0x503u))->get_behavior_interface();
        if (Behavior != nullptr)
            Behavior->update_current_behavior_cruise(false);
    }

    // 0x00545fb2..0x00545fb5: EquippedObj's slot 102 (FUN_005339e0), called directly.
    FClientEquippedObj::Slot198(Damage);

    // 0x00545fba..0x0054603b: the player's ship -- CSimple::get_relative_health against 0.2f
    // (DAT_005dfb40; FCOMP / TEST AH,0x1 / JNZ: the jump is taken on strictly-less). At or
    // above it the "hull breach" latch +0x154 clears and FUN_00553250 runs; below it, the
    // first time (latch clear) and only when the list is not a destroyed one (+0xc byte), the
    // latch is set and the "hull_breach_imminent" voice line (DAT_00678b50) is queued.
    if (this == GetPlayerObjRW())
    {
        CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, 0x503u));
        if (!(Simple->get_relative_health() < 0.2f))
        {
            Field_0x154 = 0;
            StarSysNoteDamage(this, &Damage);
            return;
        }
        if (Field_0x154 == 0 && !Damage.bDestroyed)
        {
            Field_0x154 = 1;
            NnVoiceEnqueue(G_ShipVoiceHullBreachId, 0);
        }
    }

    // 0x0054603b..0x0054603d: FUN_00553250(this, Damage).
    StarSysNoteDamage(this, &Damage);
}

void FClientShipGameobj::Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_00545e90 (RET 0x10). 0x00545ea4..0x00545eaa: EquippedObj's slot 104 (FUN_005340b0)
    // with the four arguments.
    FClientEquippedObj::Slot1a0(Contact, B, Other, D);
    // 0x00545eaf..0x00545ec3: the player's ship with a live HUD_DamageIndicator (DAT_00674a10)
    // -> FUN_004d6d40(Contact): the arrow towards the contact point.
    if (this != GetPlayerObjRW())
        return;
    HUD_DamageIndicator* const Indicator = GetHudDamageIndicator();
    if (Indicator == nullptr)
        return;
    Indicator->FlashFromPosition(static_cast<Vector const*>(Contact));
}

void FClientShipGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_00545ed0 (RET 0xc). 0x00545ee3..0x00545ee8: EquippedObj's slot 105 (FUN_00534190).
    FClientEquippedObj::Slot1a4(A, B, Munition);

    // 0x00545eed..0x00545f07: the hit sub-object id (B's low word) -- a shield equipment id
    // (SubObjectID::IsShieldEquipID) sets bit 1 of +0x18c, anything else bit 0.
    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    if (SubObjectID::IsShieldEquipID(SubObj))
        Field_0x18c |= 2;
    else
        Field_0x18c |= 1;

    // 0x00545f0d..0x00545f33: the player's ship with a live HUD_DamageIndicator -> the
    // munition's velocity (its vt+4, into a local) and FUN_004d6cd0(A, &velocity).
    if (this != GetPlayerObjRW())
        return;
    if (GetHudDamageIndicator() == nullptr)
        return;
    Vector const Velocity = Munition->get_velocity();
    GetHudDamageIndicator()->FlashAlongRay(static_cast<Vector const*>(A), &Velocity);
}

void FClientShipGameobj::Slot1b0(Vector const* Pos)
{
    // LAB_00545d80 (RET 4). 0x00545d86..0x00545e1c: the +0xf4 companion's stop prelude
    // (particle slot 1, sound vt+0x94(-1, identity pose) and vt+0x60, light FUN_005379b0 and
    // vt+8); 0x00545e1c..0x00545e7d: its release (particle FUN_004f7a90, sound vt+8, light
    // FUN_00537ad0, each slot zeroed) and +0xf4 = 0 -- the same inline sequences the two
    // helpers carry.
    CliEffectCompanionArmStop(Field_0xf4);
    CliEffectCompanionClear(Field_0xf4);
    // 0x00545e76..0x00545e83: EquippedObj's slot 108 (FUN_00533be0).
    FClientEquippedObj::Slot1b0(Pos);
}

void FClientShipGameobj::Slot1b8(float Dt)
{
    // LAB_005455b0 (RET 4) -- the lazy nebula lookup. 0x005455b0..0x005455c3: +0x34 += Dt.
    Field_0x34 += Dt;

    // 0x005455ba..0x005455d6: jump states 2 and 3 (in the tunnel) -> no nebula, no factor.
    if (Field_0x158 == 2 || Field_0x158 == 3)
    {
        DrawStateA = 0;
        DrawStateB = 0;
        return;
    }

    // 0x005455d9..0x00545612: the refresh period -- 1.0 for other ships; for the player's
    // ship 0.1 while the fog factor (+0x3c, a float) is strictly between 0.0 and 1.0
    // (FCOMP 0.0 / TEST AH,0x41 / JNZ: not-greater jumps to 0.5; FCOMP 1.0 / TEST AH,0x5 /
    // JNP: strictly-less keeps 0.1), else 0.5.
    float Period = 1.0f;
    if (this == GetPlayerObjRW())
    {
        float const Factor = *reinterpret_cast<float const*>(&DrawStateB);
        if (Factor > 0.0f && Factor < 1.0f)
            Period = 0.1f;
        else
            Period = 0.5f;
    }

    // 0x0054561a..0x00545626: TEST AH,0x41 / JNZ -- only a clock strictly above the period
    // goes on.
    if (!(Field_0x34 > Period))
        return;

    // 0x00545628..0x00545643: FUN_005553b0(get_position() (vt+0), &+0x3c, hint +0x38) into
    // +0x38.
    FNebulaEffect* const Hint = reinterpret_cast<FNebulaEffect*>(static_cast<uintptr_t>(DrawStateA));
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), reinterpret_cast<float*>(&DrawStateB), Hint);
    DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));

    // 0x0054563c..0x0054565f: the clock reloaded; above the period it is reduced on the FPU
    // stack (FSUB / FCOM / TEST AH,0x41 / JZ loops while still greater) and stored once.
    double Clock = Field_0x34;
    if (!(Clock > Period))
        return;
    do
    {
        Clock -= Period;
    } while (Clock > Period);
    Field_0x34 = static_cast<float>(Clock);
}

bool FClientShipGameobj::Slot1c0(unsigned SourceId, ID_String const* FuseId,
                                 unsigned short SubObjId, float AtT, float Lifetime)
{
    // FUN_00545a90 (RET 0x14). 0x00545a93..0x00545abe: FUN_00534240 called directly with the
    // same five arguments; not lit -> that result.
    bool const bLit = FClientEquippedObj::Slot1c0(SourceId, FuseId, SubObjId, AtT, Lifetime);
    if (!bLit)
        return bLit;
    // 0x00545ac4..0x00545ace: only DEATH_COMM_FUSE goes on.
    if (FuseId->Id != DEATH_COMM_FUSE.Id)
        return bLit;

    // 0x00545ad4..0x00545afe: the ship (class 0x503; null otherwise, read anyway).
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));

    // 0x00545afe..0x00545b9a: the formation followers (+0x1d4..+0x1d8, 8-byte watchers whose
    // first dword is the Watchable); a follower whose id is the player's ship (FUN_00548eb0):
    // its object (vtable +0x150) narrowed by 0x503 (null otherwise, used anyway) ->
    // CEqObj::get_behavior_interface; non-null -> IBehaviorManager slot 0 with {7, &this id}.
    for (std::vector<FObjRWWatcher>::const_iterator It = Ship->FormationFollowers.begin();
         It != Ship->FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr || !IsPlayerShipId(Follower->get_id()))
            continue;
        CObject* const FollowerObj = const_cast<CObject*>(Follower->cobject());
        CEqObj* const FollowerShip =
            (FollowerObj != nullptr &&
             (static_cast<unsigned>(FollowerObj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(FollowerObj)
                : nullptr;
        IBehaviorManager* const Behavior = FollowerShip->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        unsigned SelfId = get_id();
        FBehaviorManagerEvent Event;
        Event.Type = 7;
        Event.Data = &SelfId;
        Behavior->Slot00(&Event);
    }

    // 0x00545ba0..0x00545c3c: the same over the follow followers (+0x1f8..+0x1fc).
    for (std::vector<FObjRWWatcher>::const_iterator It = Ship->FollowFollowers.begin();
         It != Ship->FollowFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr || !IsPlayerShipId(Follower->get_id()))
            continue;
        CObject* const FollowerObj = const_cast<CObject*>(Follower->cobject());
        CEqObj* const FollowerShip =
            (FollowerObj != nullptr &&
             (static_cast<unsigned>(FollowerObj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(FollowerObj)
                : nullptr;
        IBehaviorManager* const Behavior = FollowerShip->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        unsigned SelfId = get_id();
        FBehaviorManagerEvent Event;
        Event.Type = 7;
        Event.Data = &SelfId;
        Behavior->Slot00(&Event);
    }

    // 0x00545c42..0x00545ca2: the player's gameobj (FUN_0054baf0) with a ship object whose
    // CShip::get_target is this ship (ids compared) -> the player's IObjAffect face (+4)
    // set_target(0, SUBOBJ_ID_NONE, DAT_00673344).
    FClientGameobj* const Player = static_cast<FClientGameobj*>(GetPlayerObjRW());
    if (Player == nullptr)
        return bLit;
    CObject* const PlayerObj = Player->Object;
    if (PlayerObj == nullptr ||
        (static_cast<unsigned>(PlayerObj->ObjectClass) & 0x503u) != 0x503u)
        return bLit;
    IObjRW* const Target = static_cast<CShip*>(PlayerObj)->get_target();
    if (Target == nullptr)
        return bLit;
    if (Target->get_id() != get_id())
        return bLit;
    static_cast<IObjAffect*>(Player)->set_target(nullptr, SUBOBJ_ID_NONE,
                                                 static_cast<int>(GetPlayerConnectionId()));
    return bLit;
}

unsigned char FClientShipGameobj::Slot1cc(FCliEquipFirePacket const* Packet)
{
    // FUN_00544d70 -- ported as CliShipHandleEquipUpdate (cli_projectile_fx.cpp).
    return CliShipHandleEquipUpdate(this, Packet);
}

void FClientShipGameobj::Slot1d0()
{
    // LAB_00545cc0 (bare RET) -- the "fire failed" beep. 0x00545cc3..0x00545cd4: FCOMP 0.0 /
    // TEST AH,0x41 / JP -- a cooldown (+0x150) strictly above zero (or unordered) returns.
    if (!(Field_0x150 <= 0.0f))
        return;

    // 0x00545cda..0x00545d00: the FIRE_FAILED_SOUND archetype, looked up once (FUN_0042ae40)
    // into +0x148 and acquired (its vt+0xc) when found.
    if (Field_0x148 == nullptr)
    {
        Field_0x148 = (Archetype::ISound*)LookupClientSoundArch(&FIRE_FAILED_SOUND);
        if (Field_0x148 != nullptr)
            AcquireClientSoundArch(Field_0x148);
    }

    // 0x00545d03..0x00545d0c: no archetype -> only the cooldown.
    if (Field_0x148 != nullptr)
    {
        if (Field_0x14c == nullptr)
        {
            // 0x00545d0e..0x00545d38: no source yet -> FUN_00428680(arch, 0, &+0x14c, 0), then
            // the cooldown.
            PlayClientSoundArch(Field_0x148, nullptr, reinterpret_cast<void**>(&Field_0x14c), 0);
            Field_0x150 = FIRE_FAILED_DELAY;
            return;
        }
        if (Field_0x14c->IsActive())
        {
            // 0x00545d3a..0x00545d5c: still playing -> vt+0x64 SetStartOffset(0) (rewind),
            // then the cooldown.
            Field_0x14c->SetStartOffset(0);
            Field_0x150 = FIRE_FAILED_DELAY;
            return;
        }
        // 0x00545d5d..0x00545d66: stopped -> vt+0x58 Start(arch, 0).
        Field_0x14c->Start(Field_0x148, 0);
    }

    // 0x00545d69..0x00545d70: +0x150 = FIRE_FAILED_DELAY.
    Field_0x150 = FIRE_FAILED_DELAY;
}

int FClientShipGameobj::set_throttle(float Throttle)
{
    // LAB_00546640: CShip::set_throttle on the object narrowed by 0x503, no null test.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->set_throttle(Throttle);
    return 0;
}

int FClientShipGameobj::set_axis_throttle(Vector const& AxisThrottle)
{
    // LAB_00546680: CSteering::SetAxisThrottle on the CShip's +0x230.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->Steering.SetAxisThrottle(AxisThrottle);
    return 0;
}

int FClientShipGameobj::set_nudge_vec(Vector const& NudgeVec)
{
    // LAB_005466c0.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->set_nudge_vec(NudgeVec);
    return 0;
}

int FClientShipGameobj::set_strafe_dir(StrafeDir Dir)
{
    // LAB_00546700 (IObjAffect slot 3, RET 4). 0x00546705..0x00546739: unchanged
    // (CShip::get_strafe_dir on the object narrowed by 0x503, no test) -> 0.
    if (static_cast<CShip*>(NarrowObject(this, 0x503u))->get_strafe_dir() == Dir)
        return 0;

    // 0x0054673f..0x005467a2: the engine (CEquipManager::FindFirst(0x20000) on +0xe4); one that
    // is not active (vt+4) is switched on through this face's vt+0x4c slot_4c(&its id, true,
    // DAT_00673344).
    CEquip* const Engine =
        static_cast<CEqObj*>(NarrowObject(this, 0x503u))->EquipManager.FindFirst(0x20000u);
    if (Engine != nullptr && !Engine->IsActive())
    {
        unsigned short EngineId = Engine->Id;
        static_cast<IObjAffect*>(this)->slot_4c(&EngineId, true,
                                                static_cast<int>(GetPlayerConnectionId()));
    }

    // 0x005467a5..0x005467d2: CShip::set_strafe_dir(Dir); always 0.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->set_strafe_dir(Dir);
    return 0;
}

int FClientShipGameobj::set_formation_leader(IObjRW* Leader)
{
    // LAB_00547140: the CShip's +0x1b8 BaseWatcher; a leader other than the one watched now
    // is set on the leader's Watchable (+8), null for a null leader -- BaseWatcher::set is the
    // inline compare + set_pointer at 0x0054717d..00547182.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
    Watchable const* const Target = (Leader != nullptr) ? static_cast<Watchable const*>(Leader)
                                                        : nullptr;
    Ship->FormationLeader.set(Target);
    return 0;
}

int FClientShipGameobj::set_formation_offset(Vector const& Offset)
{
    // LAB_005470f0: three dwords into the CShip's +0x1c0.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->FormationOffset = Offset;
    return 0;
}

int FClientShipGameobj::add_formation_follower(IObjRW* Follower)
{
    // LAB_00547190: tail JMP into CShip::add_formation_follower.
    return static_cast<int>(
        static_cast<CShip*>(NarrowObject(this, 0x503u))->add_formation_follower(Follower));
}

int FClientShipGameobj::remove_formation_follower(IObjRW* Follower)
{
    // LAB_005471c0: tail JMP into CShip::remove_formation_follower.
    return static_cast<int>(
        static_cast<CShip*>(NarrowObject(this, 0x503u))->remove_formation_follower(Follower));
}

int FClientShipGameobj::slot_38(unsigned short EquipId, int Count, int SourceClient)
{
    // FUN_005467e0 (IObjAffect slot 14, RET 0xc) -- cargo jettison.
    // 0x005467e4..0x00546836: from this client (-1 or DAT_00673344) the request goes to the
    // server: IServer vt+0x20 JettisonCargo(DAT_00673344, {get_id, EquipId, Count}); 0.
    if (SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId())
    {
        XJettisonCargo Info;
        Info.ObjectId = get_id();
        Info.ItemId = EquipId;
        Info.Count = static_cast<unsigned int>(Count);
        LocalServer()->JettisonCargo(GetPlayerConnectionId(), Info);
        return 0;
    }

    // 0x00546839..0x005468cd: from the server -- the cargo with that id (FindByID on +0xe4 of
    // the object narrowed by 0x503, CECargo::cast); none -> 0. Otherwise
    // CECargo::RemoveFromStack(Count), CShip::open_bay, one more bay close to run (+0xe8) and
    // the close countdown (+0xe4) = open_bay's result + JETTISON_CLEARANCE_TIME + itself (FADD
    // order kept).
    CEquip* const Equip =
        static_cast<CEqObj*>(NarrowObject(this, 0x503u))->EquipManager.FindByID(EquipId);
    CECargo* const Cargo = CECargo::cast(Equip);
    if (Cargo == nullptr)
        return 0;
    Cargo->RemoveFromStack(static_cast<unsigned>(Count));
    float const BayTime = static_cast<CShip*>(NarrowObject(this, 0x503u))->open_bay();
    ++Field_0xe8;
    Field_0xe4 = static_cast<float>(
        (static_cast<double>(BayTime) + CShip::JETTISON_CLEARANCE_TIME) + Field_0xe4);
    return 0;
}

int FClientShipGameobj::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    // LAB_005468e0 -- ported as CliShipSetTarget (cli_projectile_fx.cpp).
    return CliShipSetTarget(static_cast<IObjAffect*>(this), Target, SubTarget, SourceClient);
}

int FClientShipGameobj::slot_50(bool bEngage, bool bArg2, int SourceClient)
{
    // LAB_00546a10 (IObjAffect slot 20, RET 0xc) -- cruise.
    // 0x00546a16..0x00546a3e: this client's own request (-1 or DAT_00673344).
    bool const bLocal =
        SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    bool bChanged = false;
    // 0x00546a43..0x00546a53: in single player only this client's own requests go on; -5.
    if (SinglePlayer() && !bLocal)
        return -5;

    // 0x00546a59..0x00546aa0: the engine (FindFirst(0x20000), CEEngine::cast); none -> -2.
    CEEngine* const Engine = CEEngine::cast(
        static_cast<CEqObj*>(NarrowObject(this, 0x503u))->EquipManager.FindFirst(0x20000u));
    if (Engine == nullptr)
        return -2;

    // 0x00546aa6..0x00546ada: a cruise state (+0x54) that differs: changed; engaging from this
    // client first stops the thrusters (this face's vt+0x54 slot_54(false, DAT_00673344)),
    // then CEEngine::EngageCruise(bEngage, bArg2).
    if (Engine->bCruiseActive != static_cast<unsigned char>(bEngage))
    {
        bChanged = true;
        if (bEngage && bLocal)
            static_cast<IObjAffect*>(this)->slot_54(false,
                                                    static_cast<int>(GetPlayerConnectionId()));
        Engine->EngageCruise(bEngage, bArg2);
    }

    // 0x00546ae0..0x00546b1c: changed by this client -> IServer vt+8 ActivateCruise(
    // DAT_00673344, {get_id, bEngage, bArg2}); 0.
    if (bChanged && bLocal)
    {
        XActivateCruise Info;
        Info.ObjectId = get_id();
        Info.Field_0x05 = static_cast<unsigned char>(bArg2);
        Info.bActivate = bEngage;
        LocalServer()->ActivateCruise(GetPlayerConnectionId(), Info);
    }
    return 0;
}

int FClientShipGameobj::slot_54(bool bActivate, int SourceClient)
{
    // LAB_00546b50 (IObjAffect slot 21, RET 8) -- the thrusters.
    // 0x00546b54..0x00546b79: this client's own request (-1 or DAT_00673344).
    bool const bLocal =
        SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    bool bChanged = false;
    // 0x00546b7e..0x00546b8e: in single player only this client's own requests go on; -5.
    if (SinglePlayer() && !bLocal)
        return -5;

    // 0x00546b94..0x00546bd5: the engine (FindFirst(0x20000), not cast); none -> -5.
    CEquip* const Engine =
        static_cast<CEqObj*>(NarrowObject(this, 0x503u))->EquipManager.FindFirst(0x20000u);
    if (Engine == nullptr)
        return -5;
    // 0x00546bd7..0x00546bde: its cruise byte (+0x54) and vt+4 IsActive.
    unsigned char const bCruise = static_cast<CEEngine*>(Engine)->bCruiseActive;
    bool const bEngineActive = Engine->IsActive();
    if (bLocal)
    {
        // 0x00546be9..0x00546c00: switching on while cruising -> -5.
        if (bActivate && bCruise != 0)
            return -5;
        // 0x00546c03..0x00546c2d: switching on with the engine off -> this face's vt+0x4c
        // slot_4c(&engine id, true, SourceClient).
        if (bActivate && !bEngineActive)
        {
            unsigned short EngineId = Engine->Id;
            static_cast<IObjAffect*>(this)->slot_4c(&EngineId, true, SourceClient);
        }
    }

    // 0x00546c30..0x00546ca6: every thruster (CEquipTraverser(0x400), Traverse on +0xe4) whose
    // IsActive differs from bActivate: changed, vt+0x30 Activate(bActivate).
    CEquipTraverser Walk(0x400);
    for (;;)
    {
        CEquip* const Thruster =
            static_cast<CEqObj*>(NarrowObject(this, 0x503u))->EquipManager.Traverse(Walk);
        if (Thruster == nullptr)
            break;
        if (Thruster->IsActive() == bActivate)
            continue;
        bChanged = true;
        Thruster->Activate(bActivate);
    }

    // 0x00546ca8..0x00546cde: changed by this client -> IServer vt+0xc ActivateThrusters(
    // DAT_00673344, {get_id, bActivate}); 0.
    if (bChanged && bLocal)
    {
        XActivateThrusters Info;
        Info.ObjectId = get_id();
        Info.bActivate = bActivate;
        LocalServer()->ActivateThrusters(GetPlayerConnectionId(), Info);
    }
    return 0;
}

int FClientShipGameobj::slot_5c(int RequestType, unsigned const* TargetId,
                                unsigned const* SecondId, int bFlag, RequestCB* Callback)
{
    // LAB_00546df0: keep the callback at +0xec, then IServer::RequestEvent (vt+0xf0) with
    // (RequestType, get_id(), *TargetId, *SecondId, bFlag, DAT_00673344). get_id is called
    // after both ids are read (0x00546e22).
    Field_0xec = Callback;
    unsigned const Second = *SecondId;
    unsigned const Target = *TargetId;
    unsigned const SelfId = get_id();
    LocalServer()->RequestEvent(RequestType, SelfId, Target, Second,
                                static_cast<unsigned long>(bFlag), GetPlayerConnectionId());
    return 0;
}

int FClientShipGameobj::slot_60(int RequestType, unsigned const* TargetId, int Arg3)
{
    // LAB_00546e40: drop the callback, then IServer::RequestCancel (vt+0xf4) with
    // (RequestType, get_id(), *TargetId, Arg3, DAT_00673344).
    Field_0xec = nullptr;
    unsigned const Target = *TargetId;
    unsigned const SelfId = get_id();
    LocalServer()->RequestCancel(RequestType, SelfId, Target, static_cast<unsigned long>(Arg3),
                                 GetPlayerConnectionId());
    return 0;
}

int FClientShipGameobj::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                                     bool bFastExit, float Speed)
{
    // LAB_00546e90 (IObjAffect slot 25, RET 0x10) -- listing 00546e90..00546f74.
    // 0x00546e95..0x00546eab: no exit ring -> -4 (only the exit ring is tested).
    if (ExitRing == nullptr)
        return -4;

    // 0x00546eae..0x00546eed: CShip::go_tradelane on the narrowed CShip (no null test of the
    // narrowing) with this gameobj as the tradelane object; non-zero -> -5.
    IObjRW* const Self = static_cast<IObjRW*>(this);
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
    if (Ship->go_tradelane(EntranceRing, ExitRing, Self, bFastExit, Speed) != 0)
        return -5;

    // 0x00546eef..0x00546f28: IServer::GoTradelane (vt+0x18) with {own id, entrance id, exit
    // id, bFastExit} and DAT_00673344; the ids are read in that order.
    XGoTradelane Info;
    Info.ObjectId = Self->get_id();
    Info.TradelaneRing1 = EntranceRing->get_id();
    Info.TradelaneRing2 = ExitRing->get_id();
    Info.Field_0x0c = bFastExit;
    LocalServer()->GoTradelane(GetPlayerConnectionId(), Info);

    // 0x00546f2b..0x00546f59: both rings through the ObjDB (FUN_005416c0(id, 2)), then
    // FUN_00515890(Entrance, Exit, this, 0) -- its result is not used.
    FClientGameobj* const EntranceGo = ClientObjDb_FindObject(EntranceRing->get_id(), 2);
    FClientGameobj* const ExitGo = ClientObjDb_FindObject(ExitRing->get_id(), 2);
    TradeLaneShipGoHandler(this, EntranceGo, ExitGo);
    return 0;
}

int FClientShipGameobj::stop_tradelane()
{
    // LAB_00546f80 (IObjAffect slot 26, RET) -- listing 00546f80..005470ea.
    // 0x00546f86..0x00546fb3: CShip::stop_tradelane on the narrowed CShip (no null test).
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
    Ship->stop_tradelane();

    // 0x00546fb9..0x0054705f: the ship's own behavior manager (not null-tested) hears event 4,
    // {0, 0, 0, pad, 0.0f, 0, own id, entrance ring, 0} -- the same block the server's
    // notify_stop_tradelane builds.
    IBehaviorManager* const Behavior = static_cast<CEqObj*>(Ship)->get_behavior_interface();
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 0;
    Block.bExitRequested = 0;
    Block.bFastExit = 0;
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = get_id();
    Block.EntranceId = Ship->get_tradelane_entrance_ring();
    Block.ExitId = 0;
    FBehaviorManagerEvent Event;
    Event.Type = 4;
    Event.Data = &Block;
    Behavior->Slot00(&Event);

    // 0x00547061..0x005470d5: IServer::StopTradelane (vt+0x1c) with DAT_00673344, own id,
    // entrance ring, exit ring (evaluated exit ring first, then entrance, then the id).
    unsigned const ExitId = Ship->get_tradelane_exit_ring();
    unsigned const EntranceId = Ship->get_tradelane_entrance_ring();
    unsigned const SelfId = get_id();
    LocalServer()->StopTradelane(GetPlayerConnectionId(), SelfId, EntranceId, ExitId);

    // 0x005470d8: FUN_00515e20 on this gameobj.
    TradeLaneShipStop(this);
    return 0;
}

int FClientShipGameobj::slot_6c(int ArgA, int ArgB)
{
    // FUN_00547250 (IObjAffect slot 27, RET 8; ArgB is not read). The player's ship only: the
    // voice line for ArgA -- 0 "nnv_dock_too_far" (DAT_00678b1c), 1
    // "nnvoice_trade_lane_disrupted" (DAT_00678b34), 5 "nnv_formation_lost" (DAT_00678b10) --
    // when its id is non-zero, queued (FUN_00489e80(id, 0)). Always 0.
    (void)ArgB;
    if (GetPlayerObjRW() != this)
        return 0;
    unsigned VoiceId = 0;
    switch (ArgA)
    {
    case 0:
        VoiceId = G_ShipVoiceDockTooFarId;
        break;
    case 1:
        VoiceId = G_ShipVoiceTradeLaneDisruptedId;
        break;
    case 5:
        VoiceId = G_ShipVoiceFormationLostId;
        break;
    default:
        return 0;
    }
    if (VoiceId != 0)
        NnVoiceEnqueue(VoiceId, 0);
    return 0;
}

void FClientShipGameobj::RequestReturned(int Result, int Code, int Extra)
{
    // FUN_00546d00 (thiscall, RET 0xc). 0x00546d04..0x00546d36: no pending callback (+0xec)
    // -> the "stale request detected" warning (ship.cpp line 0x863).
    if (Field_0xec == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kShipSourceFile, 0x863,
              "stale request detected");
        return;
    }
    // 0x00546d39..0x00546d50: the callback's slot 0 (Result, Code, Extra); false drops it.
    if (!Field_0xec->OnResult(Result, Code, Extra))
        Field_0xec = nullptr;

    // 0x00546d5a..0x00546d61: the player's ship only.
    if (GetPlayerObjRW() != this)
        return;

    // 0x00546d63..0x00546da3: the docking voice line -- Result 0: Code 0 "dock_denied_hostile",
    // 1 "dock_denied_locked", 2 / 5 "cannot_dock", 3 "waiting_to_dock", 4 "dock_granted";
    // Result 2: Code 4 "dock_granted", 3 nothing, 0 / 1 / 2 / 5 "cannot_dock"; any other
    // Result or a Code above 5 (JA): nothing.
    unsigned VoiceId = 0;
    if (Result == 0)
    {
        switch (Code)
        {
        case 0:
            VoiceId = G_ShipVoiceDockDeniedHostileId;
            break;
        case 1:
            VoiceId = G_ShipVoiceDockDeniedLockedId;
            break;
        case 2:
        case 5:
            VoiceId = G_ShipVoiceCannotDockId;
            break;
        case 3:
            VoiceId = G_ShipVoiceWaitingToDockId;
            break;
        case 4:
            VoiceId = G_ShipVoiceDockGrantedId;
            break;
        default:
            return;
        }
    }
    else if (Result == 2)
    {
        switch (Code)
        {
        case 0:
        case 1:
        case 2:
        case 5:
            VoiceId = G_ShipVoiceCannotDockId;
            break;
        case 4:
            VoiceId = G_ShipVoiceDockGrantedId;
            break;
        default:
            return;
        }
    }
    else
    {
        return;
    }
    // 0x00546da8..0x00546daf: a non-zero id -> FUN_00489e80(id, 0).
    if (VoiceId != 0)
        NnVoiceEnqueue(VoiceId, 0);
}

int FClientShipGameobj::slot_70(unsigned Id)
{
    // LAB_005471f0: IServer vt+0xc4 (LaunchComplete) with (Id, get_id()).
    unsigned const SelfId = get_id();
    LocalServer()->LaunchComplete(Id, SelfId);
    return 0;
}

int FClientShipGameobj::slot_74(unsigned Arg)
{
    // LAB_00547220: IServer vt+0xc8 (JumpInComplete) with (Arg, get_id()).
    unsigned const SelfId = get_id();
    LocalServer()->JumpInComplete(Arg, SelfId);
    return 0;
}

void FClientShipGameobj::RequestLand(FClientGameobj* DockGo, unsigned const* BaseId)
{
    // FUN_00543b40 (RET 8). 0x00543b5e..0x00543b71: bit 3 of the +0x18c flag byte.
    Field_0x18c |= 8;

    if (DockGo != nullptr)
    {
        // 0x00543b7d..0x00543bd8: IAction::operator new(0x50), Action_Land (FUN_00542b70) with
        // this Ship and the dock's object narrowed to a CEqObj ((class & 0x303) == 0x303, null
        // otherwise), onto the action list (+0x20).
        CObject* const DockObject = DockGo->Object;
        CEqObj* Dock = nullptr;
        if (DockObject != nullptr &&
            (static_cast<unsigned>(DockObject->ObjectClass) & 0x303u) == 0x303u)
            Dock = static_cast<CEqObj*>(DockObject);
        Actions.add(new Action_Land(this, Dock));

        // 0x00543bde..0x00543c01: the IObjAffect face (+4), slot 1 set_axis_throttle({0,0,0}).
        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        static_cast<IObjAffect*>(this)->set_axis_throttle(Zero);

        // 0x00543c04..0x00543c0b: the camera only follows the watched player ship.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
            return;

        // 0x00543c11..0x00543cc4: unless the docking camera already has the view
        // (FUN_0054bca0), it goes to the dock's "HPDockCam" 'A' hardpoint -- one stack Csys,
        // identity orientation and zero position, filled by get_hardpoint (vt+0xc0, 0 is
        // success); on success the rear and turret views close (FUN_00549e30) and the Csys goes
        // to the docking camera (FUN_0054bb90). A failed lookup leaves the camera alone.
        if (!IsDockingCameraCurrent())
        {
            Transform Csys;
            Csys.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            Csys.Translation.x = 0.0f;
            Csys.Translation.y = 0.0f;
            Csys.Translation.z = 0.0f;
            char HardpointName[0x40];
            sprintf(HardpointName, "%s%c", "HPDockCam", 0x41);
            if (DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0)
            {
                CloseRearAndTurretViewsForDocking();
                SetDockingCameraTransform(reinterpret_cast<const float*>(&Csys));
            }
        }

        // 0x00543cc7..0x00543cce: always, the docking camera's fade over DAT_005dfb34 (3.0f)
        // seconds (FUN_0054bbd0).
        BeginDockingCameraSequence(3.0f);
        return;
    }

    // 0x00543cd8..0x00543d2c: no dock.
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        // The player's ship: an ERROR for base 0 (ship.cpp:0x2a8), the base asked for anyway
        // (FUN_00548ef0), and the hard cut to a covered screen (FUN_00412070(1.0f)).
        if (*BaseId == 0)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kShipSourceFile, 0x2a8,
                  "Cannot land on BASE_ID_NONE");
        }
        SetPendingLandBase(BaseId);
        SetFadeImmediate(1.0f);
        return;
    }
    // Any other ship: slot 87 (vt+0x15c) with true.
    Slot15c(true);
}

void FClientGameobj::SetOwnerPlayer(unsigned PlayerId)
{
    // FUN_005397f0: +0x10 narrowed by (class & 3) == 3 (CMP CL,3), null otherwise; tail JMP to
    // CSimple::SetOwnerPlayer with no null test.
    static_cast<CSimple*>(NarrowObject(this, 3u))->SetOwnerPlayer(PlayerId);
}

void FClientShipGameobj::SetJumpState(int State)
{
    // FUN_005437d0 (RET 4). JA past 5 -> the warning; a jump table otherwise. The player test
    // (FUN_0054baf0) is made afresh at every use, as retail does.
    switch (State)
    {
    case 0:
    {
        // 0x005437ed..0x00543807: the effect deleted (FUN_00503a80 + free) and cleared.
        if (Field_0x188 != nullptr)
        {
            delete static_cast<FJumpShipEffect*>(Field_0x188);
            Field_0x188 = nullptr;
        }
        // 0x0054380d..0x00543851: leaving state 5 on the player's ship: control exclusion
        // cleared on its CShip (0x503, no null test) and the NeuroNet screen byte dropped.
        if (Field_0x158 == 5 &&
            static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            static_cast<CShip*>(NarrowObject(this, 0x503u))->set_control_exclusion(0);
            G_bNnScreenUp = false; // DAT_0067dcc8
        }
        break;
    }
    case 1:
    {
        // 0x0054385c..0x00543866: FUN_00503ea0.
        if (Field_0x188 != nullptr)
            static_cast<FJumpShipEffect*>(Field_0x188)->StartJumpOut();
        // 0x0054386b..0x0054387e: the player's ship takes the player camera (FUN_0054bc10).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            RequestSpaceCameraMode("player");
        // 0x00543881..0x005438ce: and control exclusion 7 (after a 0 when 7 is already set),
        // with the NeuroNet screen byte raised.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
            if (Ship->is_control_excluded(7))
                Ship->set_control_exclusion(0);
            Ship->set_control_exclusion(7);
            G_bNnScreenUp = true; // DAT_0067dcc8
        }
        // 0x005438d5..0x005438f9: every ship drops its target.
        static_cast<CShip*>(NarrowObject(this, 0x503u))->set_target(nullptr);
        break;
    }
    case 2:
    {
        // 0x00543904..0x0054390e: FUN_00504210.
        if (Field_0x188 != nullptr)
            static_cast<FJumpShipEffect*>(Field_0x188)->EnterTunnel();
        // 0x00543913..0x0054392a: the "JumpOut" action taken off the list when still there.
        IAction* const JumpOut = Actions.find("JumpOut");
        if (JumpOut != nullptr)
            Actions.remove(JumpOut);
        // 0x00543930..0x005439a2: any ship but the player's goes through slot 87 with true;
        // in single player its CShip (no null test) also gets +0x214 = 1, an AddRef/Release
        // probe, and -- when that Release does not return 1 -- no owner and no control
        // exclusion.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
        {
            Slot15c(true);
            if (SinglePlayer())
            {
                CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
                Ship->bJumpedOut = 1;               // .text:00543978 MOV byte [EDI+0x214],1
                Ship->AddRef();
                if (Ship->Release() != 1)
                {
                    Ship->SetOwnerPlayer(0);
                    Ship->set_control_exclusion(0);
                }
            }
        }
        break;
    }
    case 3:
    {
        // 0x005439ad..0x005439d6: with an effect, IDPMsgHandler::OnDisconnect on it (0x00401e90,
        // a bare RET the linker folded the effect's hook into -- nothing to do), then its +0xa4
        // (the destination system) as the pending jump system with flag 0 (FUN_00548ed0).
        if (Field_0x188 != nullptr)
        {
            unsigned const DestSystem = static_cast<FJumpShipEffect*>(Field_0x188)->Field_0xa4;
            SetPendingJumpSystem(&DestSystem, false);
        }
        break;
    }
    case 4:
    {
        // 0x005439e3..0x00543a85: with an effect, the player's character jumps in at the
        // effect's +0x74 / +0x80 (CShip::jump_in with this gameobj and the effect's +0x10) and
        // the effect's jump-in hook runs; any other ship is just beamed there (CObject vt+0x7c,
        // not attached).
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Field_0x188);
        if (Effect != nullptr)
        {
            if (get_id() == GetPlayerCharacterId())
            {
                float const Speed = Effect->JumpInTime;
                static_cast<CShip*>(NarrowObject(this, 0x503u))
                    ->jump_in(static_cast<IObjRW*>(this), Effect->JumpInPosition,
                              Effect->JumpInOrientation, Speed);
                static_cast<FJumpShipEffect*>(Field_0x188)->StartJumpIn();
            }
            else
            {
                NarrowObject(this, 0x503u)
                    ->beam_object(Effect->JumpInPosition, Effect->JumpInOrientation, false);
            }
        }
        // 0x00543a88..0x00543ab2: the target dropped, +0x15c cleared.
        static_cast<CShip*>(NarrowObject(this, 0x503u))->set_target(nullptr);
        Field_0x15c = 0;
        break;
    }
    case 5:
    {
        // 0x00543aba..0x00543ad4: FUN_00504640 with no null test; the player's character gets
        // the arrival caption (FUN_0045b310).
        static_cast<FJumpShipEffect*>(Field_0x188)->FinishJump();
        if (get_id() == GetPlayerCharacterId())
            NnArrivalTextShow();
        break;
    }
    default:
        // 0x00543ae0..0x00543b01.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kShipSourceFile, 0x272,
              "invalid jump state set in ship");
        break;
    }

    // 0x00543b04..0x00543b12: the effect's clock restarts; the state is stored.
    if (Field_0x188 != nullptr)
        static_cast<FJumpShipEffect*>(Field_0x188)->Field_0x70 = 0.0f;
    Field_0x158 = State;
}

void FClientShipGameobj::BeginJumpOut(FClientGameobj* Gate)
{
    // FUN_00543530 (RET 4). 0x0054354c..0x00543566: any previous effect deleted.
    if (Field_0x188 != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(Field_0x188);
        Field_0x188 = nullptr;
    }
    // 0x00543570..0x005435a8: operator new(0xdc) + FUN_00503440(this, Gate); null on a failed
    // new.
    Field_0x188 = new (std::nothrow) FJumpShipEffect(this, static_cast<FClientSolarGameobj*>(Gate));

    // 0x005435ae..0x005435c1: the gate's type (vt+0x88 get_type), 0 without a gate.
    unsigned GateType = 0;
    if (Gate != nullptr)
        Gate->get_type(GateType);

    // 0x005435c7..0x005435cb: state 1.
    SetJumpState(1);

    // 0x005435d0..0x00543616: IAction::operator new(0x64) + FUN_00542f10(this, the effect's
    // JumpOutTime (+4, no null test), type), added to the action list.
    Actions.add(new Action_JumpOut(this, static_cast<FJumpShipEffect*>(Field_0x188)->JumpOutTime,
                                   GateType));
}

void FClientShipGameobj::BeginJumpIn(Vector const* Pos, Matrix const* Orient)
{
    // FUN_00543630 (RET 8). 0x0054364b.
    Field_0x15c = 1;

    // 0x00543652..0x00543694: the player's ship: its effect (no null test) takes the exit
    // position and orientation, and the active camera's cut flag is raised (FUN_0054bd50).
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Field_0x188);
        Effect->JumpInPosition = *Pos;
        Effect->JumpInOrientation = *Orient;
        RaiseActiveSpaceCameraCut();
        return;
    }

    // 0x005436a8..0x005436c4: any other ship: the previous effect deleted.
    if (Field_0x188 != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(Field_0x188);
        Field_0x188 = nullptr;
    }
    // 0x005436ca..0x00543721: operator new(0xdc) + FUN_005038c0(this), and the exit frame
    // copied in (no null test).
    Field_0x188 = new (std::nothrow) FJumpShipEffect(this);
    FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Field_0x188);
    Effect->JumpInPosition = *Pos;
    Effect->JumpInOrientation = *Orient;
    // 0x00543723..0x0054372f: state 4.
    SetJumpState(4);
}

void FClientShipGameobj::RequestSystemSwitch(FClientGameobj* Gate)
{
    // FUN_00543750 (RET 4): Gate's object narrowed by 0x303 (null otherwise, no later test);
    // its +0x1b4 into a local, FUN_00548ed0(&local, 0).
    CSolar const* const Solar = static_cast<CSolar const*>(NarrowObject(Gate, 0x303u));
    unsigned const DestSystem = Solar->DestSystem;
    SetPendingJumpSystem(&DestSystem, false);
}

void FClientShipGameobj::AirlockIn(Vector const& Pos, Matrix const& Orient)
{
    // FUN_005437a0: `this` narrowed by 0x503 (null otherwise), tail JMP to CShip::airlock_in.
    static_cast<CShip*>(NarrowObject(this, 0x503u))->airlock_in(Pos, Orient);
}

// The launch camera's stack Csys: Matrix at +0x10, Vector at +0x34 of the frame, i.e. the
// orientation followed directly by the position -- FUN_0054bb90's twelve floats.
static_assert(offsetof(Transform, Translation) == 0x24, "Csys12: position after 3x3");
static_assert(sizeof(Transform) == 0x30, "Csys12: twelve floats");

void FClientShipGameobj::LaunchFromDock(FClientGameobj* DockGo, int LaunchIndex)
{
    // FUN_00543d50. 0x00543d5a: vt+0x20 get_id against the player's character id.
    if (get_id() != GetPlayerCharacterId())
        return;

    // 0x00543d70..0x00543d93: the dock narrowed to a CEqObj ((class & 0x103) == 0x103), null
    // when the gameobj or its object is missing.
    CEqObj* DockEqObj = nullptr;
    if (DockGo != nullptr)
    {
        CObject* const DockObj = DockGo->Object;
        if (DockObj != nullptr &&
            (static_cast<unsigned>(DockObj->ObjectClass) & 0x103u) == 0x103u)
            DockEqObj = static_cast<CEqObj*>(DockObj);
    }

    // 0x00543d95..0x00543db4: this gameobj's own object narrowed to a CShip (0x503). Retail
    // calls CShip::launch through the result with no null test (0x00543dc0).
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, 0x503u));
    Ship->launch(this, DockEqObj, LaunchIndex);

    // 0x00543dc6: PUSH 0x401c0000 / PUSH 0 -- the double 7.0.
    StarSysSuppressPlayerCollisions(7.0);

    // 0x00543dd5: the camera only follows the watched player ship.
    if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
        return;

    // 0x00543dfb..0x00543e53: one Csys on the stack -- identity orientation at +0x10, zero
    // position at +0x34 -- initialised once, before the first lookup. Both get_hardpoint
    // calls write into the same pair, so a failed first lookup leaves whatever it wrote for
    // the second.
    Transform Csys;
    Csys.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Csys.Translation.x = 0.0f;
    Csys.Translation.y = 0.0f;
    Csys.Translation.z = 0.0f;

    // 0x00543de8: the hardpoint suffix is 'A' + index, printed with "%s%c" (.rdata 005dffa8).
    char HardpointName[0x40];
    int const Suffix = LaunchIndex + 0x41;
    sprintf(HardpointName, "%s%c", "HPLaunchCam", Suffix);

    // 0x00543e73: dock vt+0xc0 get_hardpoint(Name, &Pos, &Orient); 0 is success. No null
    // test on the dock here.
    bool bFound = DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0;
    if (!bFound)
    {
        // 0x00543e85: fall back to the docking hardpoint of the same index.
        sprintf(HardpointName, "%s%c", "HPDockCam", Suffix);
        bFound = DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0;
    }

    CloseRearAndTurretViewsForDocking(); // FUN_00549e30, on both arms (0x00543ebf / 0x00543eda)
    if (bFound)
    {
        // 0x00543ec9: FUN_0054bb90(&Csys) -- orientation then position, twelve floats.
        SetDockingCameraTransform(reinterpret_cast<const float*>(&Csys));
    }
    else
    {
        // 0x00543ee4: FUN_0054bc10("player").
        RequestSpaceCameraMode("player");
    }
}

void CloseRearAndTurretViewsForDocking()
{
    // FUN_00549e30. DAT_00674aa0 is read twice with no null test; each byte is compared with 1.
    // 0x00549e35: +0x5ba (rear view) -> FUN_005763b0(0x39, 0).
    if (GetHudStatus()->bRearView == 1)
        HudPostCommand(0x39, 0);
    // 0x00549e50: +0x5b9 (turret view) -> FUN_005763b0(0xbf, 0).
    if (GetHudStatus()->bTurretView == 1)
        HudPostCommand(0xbf, 0);
}

// ===========================================================================
// Projectile
// ===========================================================================

FClientProjectileGameobj::FClientProjectileGameobj(EClientGameobjKind Kind)
    : FClientSimpleGameobj(Kind)
{
    // FUN_00542550: 0x00542576 +0xac = -1.0f; 0x00542582..0x00542594 +0xb0..+0xbc = 0.
    ConstEffectDelay = -1.0f;
    OneShotCompanion[0] = nullptr;
    OneShotCompanion[1] = nullptr;
    OneShotCompanion[2] = nullptr;
    OneShotCompanion[3] = nullptr;
}

FClientProjectileGameobj::~FClientProjectileGameobj()
{
    // FUN_005425f0: the companion released (particle FUN_004f7a90, sound vt+8, light
    // FUN_00537ad0, then +0xb0 = 0) by the destructor body (0x00542622..0x0054266c) and again
    // by the member's own destructor (0x00542672..0x005426b8); then GameObject's (FUN_005394b0).
    CliEffectCompanionClear(OneShotCompanion);
    CliEffectCompanionClear(OneShotCompanion);
}

namespace
{
    // The const_effect start shared by Projectile slots 92 and 93 (0x00542738..0x0054276a /
    // 0x00542815..0x00542853): the effect under the companion's +0x58 id, started through
    // FUN_00503070 on this gameobj at Xform, into OneShotCompanion (+0xb0). The id is tested
    // for emptiness first, as both slots do right before the call.
    void ProjectileStartConstEffect(FClientProjectileGameobj* Self, ID_String const* Effect,
                                    long EngineIndex, Transform const& Xform)
    {
        if (Effect->IsEmpty())
            return;
        CliCreateEffectOneShot(FindEffectArch(&Effect->Id), Self, EngineIndex,
                               reinterpret_cast<FTransform const*>(&Xform),
                               Self->OneShotCompanion);
    }
}

void FClientProjectileGameobj::Slot170()
{
    // FUN_005426e0.
    FClientGameobj::Slot170();                                                 // 0x005426e8
    // 0x005426ed..0x0054270d: the object narrowed by class & 7 (no null test before +0x88),
    // its archetype's companion, the companion's +0x58 const_effect.
    Archetype::ICliObj* const Companion = NarrowObject(this, 0x7u)->Archetype->Companion;
    ID_String const* const Effect = Archetype::CliProjConstEffect(Companion);
    if (Effect->IsEmpty())                                                     // 0x00542717
        return;
    // 0x00542719..0x00542727: a delay other than exactly 0 (NaN included) is armed.
    float const Delay = Archetype::CliProjConstEffectDelay(Companion);
    if (!(Delay == 0.0f))
    {
        ConstEffectDelay = Delay;                                              // 0x00542779
        return;
    }
    // 0x00542729..0x0054276a: Slot1c0(&Xform), get_index, start.
    Transform Xform;
    Slot1c0(&Xform);
    long const EngineIndex = get_index();
    ProjectileStartConstEffect(this, Effect, EngineIndex, Xform);
}

void FClientProjectileGameobj::update(float Dt)
{
    // FUN_00542790 (RET 4).
    if (Slot158())                                                             // 0x0054279a
        return;
    FClientGameobj::update(Dt);                                                // 0x005427af
    // 0x005427b4..0x005427c5: a delay below 0 (or NaN) is not armed.
    if (!(ConstEffectDelay >= 0.0f))
        return;
    // 0x005427cb..0x005427e6: count down; go on only at or below 0 (not above, not NaN).
    ConstEffectDelay = ConstEffectDelay - Dt;
    if (!(ConstEffectDelay <= 0.0f))
        return;
    ConstEffectDelay = -1.0f;                                                  // 0x005427f1
    Transform Xform;
    Slot1c0(&Xform);                                                           // 0x005427fb
    // 0x00542801..0x00542825: the class & 7 object's archetype companion (no null test), then
    // get_index.
    Archetype::ICliObj* const Companion = NarrowObject(this, 0x7u)->Archetype->Companion;
    long const EngineIndex = get_index();
    ProjectileStartConstEffect(this, Archetype::CliProjConstEffect(Companion), EngineIndex,
                               Xform);
}

void FClientProjectileGameobj::Slot1a0(void const* Contact, void const* B,
                                       FClientGameobj* Other, void const* D)
{
    // FUN_0053d4f0: RET 0x10.
    (void)Contact;
    (void)B;
    (void)Other;
    (void)D;
}

void FClientProjectileGameobj::Slot1b0(Vector const* Pos)
{
    // FUN_00542870 (RET 4): FUN_004fcef0(&EffectList, &OneShotCompanion), then GameObject's
    // slot 108 FUN_0053a880 called directly.
    CliEffectListRemove(&EffectList, OneShotCompanion);
    FClientGameobj::Slot1b0(Pos);
}

void FClientProjectileGameobj::Slot1c0(Transform* Out)
{
    // LAB_005428a0 (RET 4): 1.0f at +0x00 / +0x10 / +0x20, every other dword of the 0x30 zero.
    Out->Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Out->Translation.x = 0.0f;
    Out->Translation.y = 0.0f;
    Out->Translation.z = 0.0f;
}

// ===========================================================================
// Munition
// ===========================================================================

FClientMunitionGameobj::FClientMunitionGameobj(EClientGameobjKind Kind)
    : FClientProjectileGameobj(Kind)
{
    // FUN_0052e100: FUN_00542550(Kind), then the three Munition vtables (C++).
}

FClientMunitionGameobj::~FClientMunitionGameobj()
{
    // The thunk FUN_0053d470 -> FUN_005425f0: Projectile's destructor body, nothing of its own.
}

void FClientMunitionGameobj::Slot194(void const* A, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    // FUN_0053ed80 (RET 0x10).
    Other->Slot1a4(A, D, this);                                                // 0x0053ed99
    Slot1ac(A, B, Other, D);                                                   // 0x0053edab
}

// ===========================================================================
// Beam
// ===========================================================================

FClientBeamGameobj::FClientBeamGameobj()
    : FClientMunitionGameobj(EClientGameobjKind::Beam)
{
    // 0x0052de9f..0x0052ded2: FUN_0052e100(3); FUN_005154a0(+0xc0, 0, 0, 0, 0) -- stores the
    // four and AddRefs the non-null ones, so four nulls; +0xd0 = 0; Beam's vtables (C++).
    Companion[0] = nullptr;
    Companion[1] = nullptr;
    Companion[2] = nullptr;
    Companion[3] = nullptr;
    BeamAlloc = nullptr;
}

FClientBeamGameobj::~FClientBeamGameobj()
{
    // FUN_0053f1e0: the companion at +0xc0 released (0x0053f212..0x0053f25c), the beam effect
    // at +0xd0 released and cleared (0x0053f262..0x0053f271), the companion released again by
    // its member destructor (0x0053f277..0x0053f2bd); then Projectile's (FUN_005425f0).
    CliEffectCompanionClear(Companion);
    if (BeamAlloc != nullptr)
    {
        BeamEffectRelease(BeamAlloc);
        BeamAlloc = nullptr;
    }
    CliEffectCompanionClear(Companion);
}

void FClientBeamGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053f2f0 (RET 4): FUN_00539bc0(0x67, Arch), then vt+0x4c open(Arch) on the object
    // narrowed by class & 0x67 -- no null test (0x0053f31e calls through a null object).
    GameobjCreateOfClass(this, CObject::ClassBeam, Arch);
    NarrowObject(this, 0x67u)->open(Arch);
}

bool FClientBeamGameobj::find(unsigned const& Id)
{
    // FUN_0053f330: XOR AL,AL / RET 4.
    (void)Id;
    return false;
}

void FClientBeamGameobj::update(float Dt)
{
    // LAB_0053f610 (RET 4).
    FClientProjectileGameobj::update(Dt);                                      // 0x0053f619
    // 0x0053f61e..0x0053f640: the class & 7 object's vt+0x50 update(Dt, UPDATE_DEFAULT), no
    // null test, result dropped.
    NarrowObject(this, 0x7u)->update(Dt, CObject::UPDATE_DEFAULT);
    if (Slot154())                                                             // 0x0053f647
        return;
    // 0x0053f651..0x0053f67e: the object's +0xe8 life below 0 -> Slot15c(true). TEST AH,1 /
    // JZ skips only when C0 is clear, so a NaN life (C0 set) also expires the beam.
    CProjectile const* const Projectile = static_cast<CProjectile const*>(NarrowObject(this, 0x7u));
    if (!(Projectile->Lifetime >= 0.0f))
        Slot15c(true);
}

void FClientBeamGameobj::Slot194(void const* A, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_0053f690 (RET 0x10).
    if (Slot154())                                                             // 0x0053f698
        return;
    Other->Slot1a4(A, D, this);                                                // 0x0053f6bc
    Slot1ac(A, B, Other, D);                                                   // 0x0053f6ce
    // 0x0053f6d4..0x0053f6de: whether Other is the player's ship (FUN_0054baf0).
    bool const bOtherIsPlayer = static_cast<void*>(static_cast<IObjRW*>(Other)) == GetPlayerObjRW();
    // 0x0053f6e7..0x0053f724: the CBeam's velocity (+0xf8) zeroed -- no null test.
    CBeam* const BeamObj = static_cast<CBeam*>(NarrowObject(this, 0x67u));
    BeamObj->Velocity.x = 0.0f;
    BeamObj->Velocity.y = 0.0f;
    BeamObj->Velocity.z = 0.0f;
    // 0x0053f727..0x0053f748: the beam effect marked hit (FUN_004fc8f0(1, bOtherIsPlayer)),
    // released and cleared.
    if (BeamAlloc != nullptr)
    {
        BeamEffectSetHitFlags(BeamAlloc, true, bOtherIsPlayer);
        BeamEffectRelease(BeamAlloc);
        BeamAlloc = nullptr;
    }
    // 0x0053f752..0x0053f75c: FUN_004fcef0(&EffectList, &Companion).
    CliEffectListRemove(&EffectList, Companion);
    // 0x0053f761..0x0053f764: Projectile's slot 108 FUN_00542870 at A, called directly.
    FClientProjectileGameobj::Slot1b0(static_cast<Vector const*>(A));
}

void FClientBeamGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053f780: RET 0xc.
    (void)A;
    (void)B;
    (void)Munition;
}

void FClientBeamGameobj::Slot1ac(void const* A, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_0053f790 (RET 0x10). B is not read.
    (void)B;
    // 0x0053f796..0x0053f7c0: in multiplayer only the player's own shots report -- the
    // projectile's owner (+0xe4, class & 7 object, no null test) against DAT_00673378.
    if (!SinglePlayer())
    {
        CProjectile const* const Shot = static_cast<CProjectile const*>(NarrowObject(this, 0x7u));
        if (Shot->OwnerId != GetPlayerCharacterId())
            return;
    }
    CObject* const Obj = NarrowObject(this, 0x7u);
    Vector const* const HitPos = static_cast<Vector const*>(A);
    SSPMunitionCollisionInfo Info;
    Info.Field_0x00 = Obj->Archetype->Nickname.Id;                             // 0x0053f7f9
    Info.Field_0x04 = static_cast<CProjectile const*>(Obj)->OwnerId;           // 0x0053f7e0
    Info.ObjectId = Other->get_id();                                           // 0x0053f80c
    // 0x0053f819: the low word of D, the sub-object that was hit. +0x0e is never written.
    Info.Field_0x0c = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0e = 0;
    std::memcpy(&Info.Field_0x10, &HitPos->x, sizeof(float));
    std::memcpy(&Info.Field_0x14, &HitPos->y, sizeof(float));
    std::memcpy(&Info.Field_0x18, &HitPos->z, sizeof(float));
    // 0x0053f82d..0x0053f848: DAT_0067ecd0 slot 53 (+0xd4) SPMunitionCollision(Info,
    // DAT_00673344), no null test.
    LocalServer()->SPMunitionCollision(Info, GetPlayerConnectionId());
}

unsigned FClientBeamGameobj::Slot1bc() const
{
    // LAB_0052e130: MOV EAX,0x2000000 / RET.
    return 0x2000000u;
}

// ===========================================================================
// Mine / CounterMeasure / Guided / Loot -- shared bodies
// ===========================================================================

namespace
{
    // .rdata:005df7e0 / 005df858 -- the FDUMP file arguments of Loot slot 92 (0x0053e666) and
    // Guided slot 112 (0x0053f142).
    char const kLootSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\Loot.cpp";
    char const kMunitionSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\Munition.cpp";

    // The object-update tail of Loot / Mine / Guided / CounterMeasure slot 93 (0x0053e6af..
    // 0x0053e768, 0x0053e9af.., 0x0053ecbf.., 0x0053000f..): the owner is the class & 3
    // object's +0xb4 OwnerPlayer, or FUN_0054bb00 when that narrow fails. This connection's own
    // object runs its vt+0x50 update(Dt, UPDATE_DEFAULT); anyone else's runs UPDATE_DUMB, and
    // only in multiplayer. The Mask-narrowed object is used with no null test.
    void GameobjOwnerUpdate(FClientGameobj* Self, unsigned Mask, float Dt)
    {
        CObject* const Owned = NarrowObject(Self, 0x3u);
        unsigned const Owner = (Owned != nullptr) ? static_cast<CSimple const*>(Owned)->OwnerPlayer
                                                  : GetPlayerConnectionId();
        if (Owner == GetPlayerConnectionId())
        {
            NarrowObject(Self, Mask)->update(Dt, CObject::UPDATE_DEFAULT);
            return;
        }
        if (SinglePlayer())
            return;
        NarrowObject(Self, Mask)->update(Dt, CObject::UPDATE_DUMB);
    }

    // FUN_0053ea60 (RET 4, Dt unread) -- Mine, CounterMeasure and Guided slot 110. FCOMP 0.0 /
    // TEST AH,0x44 / JP: only an exactly-zero +0x34 falls through (greater, less and NaN jump
    // out). The nebula at get_position() goes to +0x38 and its fog factor to +0x3c
    // (FUN_005553b0, no hint), then +0x34 = 1.0f.
    void ProjectileLazyNebula(FClientGameobj* Self)
    {
        if (!(Self->Field_0x34 == 0.0f))
            return;
        FNebulaEffect* const Nebula = StarSysFindNebula(
            &Self->get_position(), reinterpret_cast<float*>(&Self->DrawStateB), nullptr);
        Self->DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));
        Self->Field_0x34 = 1.0f;
    }

    // LAB_0053eaa0 (RET 0xc) -- Mine and Guided slot 105: a munition whose object is a CBeam
    // (class & 0x67) goes on to GameObject's slot 105 (the tail JMP to FUN_00539d80 with the
    // arguments rewritten in place, unchanged); a null munition, a munition without an object
    // or any other class ends here.
    bool IsBeamMunition(FClientGameobj const* Munition)
    {
        if (Munition == nullptr)
            return false;
        return NarrowObject(Munition, 0x67u) != nullptr;
    }
}

// ---- Mine ------------------------------------------------------------------

FClientMineGameobj::FClientMineGameobj()
    : FClientProjectileGameobj(EClientGameobjKind::Mine)
{
    // 0x0053ca8d..0x0053caa4: FUN_00542550(5), then the vtables 005dedcc / IObjAffect /
    // CBase 005ded44 (C++).
}

FClientMineGameobj::~FClientMineGameobj()
{
    // FUN_0053d440: thunk_FUN_005425f0, Projectile's body; nothing of Mine's own.
}

Vector FClientMineGameobj::get_velocity() const
{
    // LAB_0053eae0 (RET 4): TEST byte [this+0x44],4 -- a destroyed mine answers {0,0,0};
    // otherwise IObjInspectImpl::get_velocity (the Common import at 0x0053eafd).
    if ((Flags & 4u) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        return Zero;
    }
    return IObjInspectImpl::get_velocity();
}

void FClientMineGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053e930 (RET 4).
    GameobjCreateOfClass(this, CObject::ClassMine, Arch);
    NarrowObject(this, 0xfu)->open(Arch);
}

bool FClientMineGameobj::find(unsigned const& Id)
{
    // FUN_0053e970 (RET 4): CObject::Find(Id, 0xf) into +0x10, advise(true), SETNZ.
    Object = CObject::Find(Id, CObject::ClassMine);
    advise(true);
    return Object != nullptr;
}

void FClientMineGameobj::update(float Dt)
{
    // FUN_0053e9a0 (RET 4): Projectile's update FUN_00542790, then the owner tail on class & 0xf.
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, 0xfu, Dt);
}

void FClientMineGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053eaa0 (RET 0xc).
    if (!IsBeamMunition(Munition))
        return;
    FClientGameobj::Slot1a4(A, B, Munition);
}

void FClientMineGameobj::Slot1b8(float Dt)
{
    // FUN_0053ea60 (RET 4).
    (void)Dt;
    ProjectileLazyNebula(this);
}

// ---- CounterMeasure ----------------------------------------------------------

FClientCounterMeasureGameobj::FClientCounterMeasureGameobj()
    : FClientProjectileGameobj(EClientGameobjKind::CounterMeasure)
{
    // 0x0053cdbd..0x0053cdd4: FUN_00542550(6), then the vtables 005df01c / IObjAffect /
    // CBase 005def94 (C++).
}

FClientCounterMeasureGameobj::~FClientCounterMeasureGameobj()
{
    // FUN_0053d440: Projectile's body.
}

void FClientCounterMeasureGameobj::create(Archetype::Root* Arch)
{
    // FUN_0052ff90 (RET 4).
    GameobjCreateOfClass(this, CObject::ClassCounterMeasure, Arch);
    NarrowObject(this, 0x17u)->open(Arch);
}

bool FClientCounterMeasureGameobj::find(unsigned const& Id)
{
    // FUN_0052ffd0 (RET 4).
    Object = CObject::Find(Id, CObject::ClassCounterMeasure);
    advise(true);
    return Object != nullptr;
}

void FClientCounterMeasureGameobj::update(float Dt)
{
    // FUN_00530000 (RET 4).
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, 0x17u, Dt);
}

void FClientCounterMeasureGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053f780: RET 0xc.
    (void)A;
    (void)B;
    (void)Munition;
}

void FClientCounterMeasureGameobj::Slot1b8(float Dt)
{
    // FUN_0053ea60 (RET 4).
    (void)Dt;
    ProjectileLazyNebula(this);
}

// ---- Guided ----------------------------------------------------------------

FClientGuidedGameobj::FClientGuidedGameobj()
    : FClientMunitionGameobj(EClientGameobjKind::Guided)
{
    // 0x0053d0cd..0x0053d0e4: FUN_00542550(4) -- Projectile's ctor; Munition's ctor
    // FUN_0052e100 adds only its vtables, which Guided's overwrite at once -- then the vtables
    // 005df26c / IObjAffect 005df1ec / CBase 005df1e4 (C++).
}

FClientGuidedGameobj::~FClientGuidedGameobj()
{
    // FUN_0053d440: Projectile's body.
}

void FClientGuidedGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053ec30 (RET 4).
    GameobjCreateOfClass(this, CObject::ClassGuided, Arch);
    NarrowObject(this, 0xa7u)->open(Arch);
}

bool FClientGuidedGameobj::find(unsigned const& Id)
{
    // FUN_0053ec70 (RET 4).
    Object = CObject::Find(Id, CObject::ClassGuided);
    advise(true);
    return Object != nullptr;
}

void FClientGuidedGameobj::update(float Dt)
{
    // FUN_0053ecb0 (RET 4).
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, 0xa7u, Dt);
}

void FClientGuidedGameobj::Slot1a4(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053eaa0 (RET 0xc) -- Mine's slot 105.
    if (!IsBeamMunition(Munition))
        return;
    FClientGameobj::Slot1a4(A, B, Munition);
}

void FClientGuidedGameobj::Slot1ac(void const* A, void const* B, FClientGameobj* Other,
                                   void const* D)
{
    // LAB_0053ee00 (RET 0x10). B is not read.
    (void)B;
    // 0x0053ee07..0x0053ee3a: in multiplayer only a collision with an object this connection
    // owns reports -- Other's class & 3 object's +0xb4 OwnerPlayer, or FUN_0054bb00 when the
    // narrow fails, against DAT_00673344.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = NarrowObject(Other, 0x3u);
        unsigned const Owner = (OtherObj != nullptr)
                                   ? static_cast<CSimple const*>(OtherObj)->OwnerPlayer
                                   : GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }
    // 0x0053ee40..0x0053eec0: this class & 7 object (no null test) -- its +0xe4 owner and its
    // archetype's nickname id; Other's get_id; D's low word; the hit point A.
    CObject* const Obj = NarrowObject(this, 0x7u);
    Vector const* const HitPos = static_cast<Vector const*>(A);
    SSPMunitionCollisionInfo Info;
    Info.Field_0x04 = static_cast<CProjectile const*>(Obj)->OwnerId;           // 0x0053ee5a
    Info.Field_0x00 = Obj->Archetype->Nickname.Id;                             // 0x0053ee79
    Info.ObjectId = Other->get_id();                                           // 0x0053ee84
    Info.Field_0x0c = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D)); // 0x0053ee96
    Info.Pad_0x0e = 0; // +0x0e is never written
    std::memcpy(&Info.Field_0x10, &HitPos->x, sizeof(float));
    std::memcpy(&Info.Field_0x14, &HitPos->y, sizeof(float));
    std::memcpy(&Info.Field_0x18, &HitPos->z, sizeof(float));
    // 0x0053eea5..0x0053eec0: DAT_0067ecd0 slot 53 (+0xd4) SPMunitionCollision(Info,
    // DAT_00673344), no null test.
    LocalServer()->SPMunitionCollision(Info, GetPlayerConnectionId());
}

void FClientGuidedGameobj::Slot1b0(Vector const* Pos)
{
    // FUN_0053edc0 (RET 4). 0x0053edc3..0x0053eddd: with a const_effect particle (+0xb8),
    // FUN_004f7d20(0, -1, frame) on it -- no owner, no part, the frame this gameobj's vt+0x10
    // returns. Slot 4 (get_orientation) is ICF-folded with slot 3 (get_transform) in retail
    // (both are Common 0x5F1F0), so the pointer it hands over is the object's whole Transform.
    FParticleEffect* const Particle = static_cast<FParticleEffect*>(OneShotCompanion[2]);
    if (Particle != nullptr)
    {
        Transform const& Frame = get_transform();
        ParticleEffectAttach(Particle, nullptr, -1, reinterpret_cast<FTransform const*>(&Frame));
    }
    // 0x0053ede9: Projectile's slot 108 FUN_00542870, called directly.
    FClientProjectileGameobj::Slot1b0(Pos);
}

void FClientGuidedGameobj::Slot1b8(float Dt)
{
    // FUN_0053ea60 (RET 4).
    (void)Dt;
    ProjectileLazyNebula(this);
}

unsigned FClientGuidedGameobj::Slot1bc() const
{
    // LAB_0053d430: MOV EAX,0x1000000 / RET.
    return 0x1000000u;
}

void FClientGuidedGameobj::Slot1c0(Transform* Out)
{
    // LAB_0053f030 (RET 4).
    // 0x0053f03b..0x0053f059: the local frame -- FUN_004215b0 (identity rotation), zero
    // translation.
    Transform Local;
    Local.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Local.Translation.x = 0.0f;
    Local.Translation.y = 0.0f;
    Local.Translation.z = 0.0f;

    // 0x0053f044..0x0053f089: the class & 7 object (no null test before +0x88), its archetype's
    // companion (a CliMunition), the HP_trail_parent text at +0x60 -- tested by its first byte.
    char const* const TrailParent =
        Archetype::CliMunitionHpTrailParent(NarrowObject(this, 0x7u)->Archetype->Companion);
    if (TrailParent[0] != '\0')
    {
        // 0x0053f08f..0x0053f0d2: FindHardpoint_PS(get_index(), name, &Handle (-1), &Info).
        long Handle = -1;
        HardpointInfo Hp;
        if (FindHardpoint_PS(get_index(), TrailParent, Handle, Hp))
        {
            // 0x0053f0d4..0x0053f0fb: the hardpoint's position and orientation become the
            // frame, and the renderer is not asked.
            Local.Translation = Hp.Position;
            Local.Rotation = Hp.Orient;
            *Out = Local;
            return;
        }
        // 0x0053f100..0x0053f14f: FDUMP 0x100001, Munition.cpp line 0xe1, the raw object's
        // archetype nickname and the HP_trail_parent text (read again through the class & 7
        // object); then on to the renderer.
        char const* const TrailParentAgain =
            Archetype::CliMunitionHpTrailParent(NarrowObject(this, 0x7u)->Archetype->Companion);
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s: Missing HP_trail_parent(%s).",
              kMunitionSourceFile, 0xe1, Object->get_archetype()->Nickname.get_string(),
              TrailParentAgain);
    }

    // 0x0053f152..0x0053f1b1: the raw object's archetype mesh (+0x40) on DALib::Renderer --
    // IRenderer2 slot 4 (+0x10) the six extents and slot 5 (+0x14) the centre, both at LOD
    // 1.0; when both answer, the translation is {centre x, centre y, extents[4]}.
    float Extents[6];
    void* const MeshId =
        reinterpret_cast<void*>(static_cast<intptr_t>(Object->get_archetype()->EngineIndex));
    if (DALib::Renderer->GetBounds(MeshId, 1.0f, Extents) != 0)
    {
        float Center[3];
        void* const MeshIdAgain =
            reinterpret_cast<void*>(static_cast<intptr_t>(Object->get_archetype()->EngineIndex));
        if (DALib::Renderer->GetCentroid(MeshIdAgain, 1.0f, Center) != 0)
        {
            Local.Translation.x = Center[0];
            Local.Translation.y = Center[1];
            Local.Translation.z = Extents[4];
        }
    }
    // 0x0053f1b3..0x0053f1c5: the 0x30-byte frame copied out.
    *Out = Local;
}

int FClientGuidedGameobj::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    // LAB_0053eed0 (RET 0xc; `this` is the IObjAffect sub-object in retail). The class & 0xa7
    // object is used with no null test.
    CGuided* const Guided = static_cast<CGuided*>(NarrowObject(this, 0xa7u));
    // 0x0053eefa..0x0053ef08: a munition with no seeker (+0x90) answers -3.
    Archetype::Munition const* const MunArch =
        static_cast<Archetype::Munition const*>(Guided->Archetype);
    if (MunArch->Seeker == 0)
        return -3;

    // 0x0053ef0e..0x0053ef3b: the current target (the watcher at +0xf0, its Watchable minus
    // eight) and sub-target (+0xf8) both unchanged -> 0.
    Watchable const* const Watched = Guided->TargetWatch.target();
    IObjRW const* const Current =
        (Watched != nullptr)
            ? reinterpret_cast<IObjRW const*>(reinterpret_cast<unsigned char const*>(Watched) - 8)
            : nullptr;
    if (Current == Target && Guided->SubTarget == SubTarget)
        return 0;

    // 0x0053ef42..0x0053ef59: the request is this connection's when SourceClient is -1 or
    // FUN_0054bb00 (retail keeps the flag in Target's argument slot).
    bool const bOwnRequest =
        (SourceClient == -1) || (static_cast<unsigned>(SourceClient) == GetPlayerConnectionId());
    // 0x0053ef5e..0x0053ef6f: in single player a foreign request changes nothing.
    if (SinglePlayer() && !bOwnRequest)
        return 0;

    // 0x0053ef75..0x0053ef98: the watcher moves to Target's Watchable (IObjRW+8, null for a null
    // Target) when it differs -- BaseWatcher::set_pointer after the inline compare, which is
    // BaseWatcher::set. The seeker is re-read before each of the two stores.
    if (static_cast<Archetype::Munition const*>(Guided->Archetype)->Seeker != 0)
    {
        Watchable const* const NewWatched =
            (Target != nullptr) ? reinterpret_cast<Watchable const*>(
                                      reinterpret_cast<unsigned char const*>(Target) + 8)
                                : nullptr;
        Guided->TargetWatch.set(NewWatched);
    }
    // 0x0053ef9e..0x0053efb3: +0xf8 = SubTarget.
    if (static_cast<Archetype::Munition const*>(Guided->Archetype)->Seeker != 0)
        Guided->SubTarget = SubTarget;

    // 0x0053efba..0x0053f016: this connection's request goes to the server --
    // DAT_0067ecd0 slot 4 (+0x10) SetTarget(DAT_00673344, {this id, SUBOBJ_ID_NONE,
    // Target's id or 0, SubTarget}). The pad words are never written.
    if (bOwnRequest)
    {
        XSetTarget Info;
        Info.ObjectId = get_id();
        Info.SubObjId = SUBOBJ_ID_NONE;
        Info.Pad_0x06 = 0;
        Info.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Info.TargetSubObjId = SubTarget;
        Info.Pad_0x0e = 0;
        LocalServer()->SetTarget(GetPlayerConnectionId(), Info);
    }
    return 0;
}

// ---- Loot ------------------------------------------------------------------

FClientLootGameobj::FClientLootGameobj()
    : FClientSimpleGameobj(EClientGameobjKind::Loot)
{
    // FUN_0053e470: FUN_00547440(7), +0xac = 0 (0x0053e47a), then the vtables 005df5d4 /
    // IObjAffect / CBase 005df548 (C++).
    TractoredExplosionArch = nullptr;
}

FClientLootGameobj::~FClientLootGameobj()
{
    // FUN_0053e4e0 (ESI = this + 0xc): the tractored explosion released (FUN_00536170) when
    // there is one (0x0053e511..0x0053e529), then GameObject's body FUN_005394b0 through the
    // base chain.
    if (TractoredExplosionArch != nullptr)
        ExplosionArchRelease(TractoredExplosionArch);
}

void FClientLootGameobj::create(Archetype::Root* Arch)
{
    // LAB_0053e550 (RET 4).
    GameobjCreateOfClass(this, CObject::ClassLoot, Arch);
    NarrowObject(this, 0x803u)->open(Arch);
}

bool FClientLootGameobj::find(unsigned const& Id)
{
    // LAB_0053e5a0 (RET 4).
    Object = CObject::Find(Id, CObject::ClassLoot);
    advise(true);
    return Object != nullptr;
}

void FClientLootGameobj::Slot170()
{
    // LAB_0053e5e0.
    FClientGameobj::Slot170();                                                 // 0x0053e5e5
    // 0x0053e5ea..0x0053e61d: the class & 0x803 object's archetype (no null test), a
    // LootCrate / Equipment, and its +0x68 tractored_explosion.
    Archetype::Equipment const* const Crate =
        static_cast<Archetype::Equipment const*>(NarrowObject(this, 0x803u)->get_archetype());
    ID_String const* const ExplosionId = &Crate->TractoredExplosion;
    if (!ExplosionId->IsEmpty())
    {
        TractoredExplosionArch = Archetype::GetExplosion(*ExplosionId);        // 0x0053e620
        if (TractoredExplosionArch != nullptr)
        {
            ExplosionArchAddRef(TractoredExplosionArch);                       // 0x0053e634
        }
        else
        {
            // 0x0053e640..0x0053e673: FDUMP 0x100002, Loot.cpp line 0x47, the raw object's
            // archetype nickname and the explosion id's string (fetched first).
            char const* const ExplName = ExplosionId->get_string();
            char const* const ArchName = Object->get_archetype()->Nickname.get_string();
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Archtype(%s) tractored_explosion(%s) not found.",
                  kLootSourceFile, 0x47, ArchName, ExplName);
        }
    }
    // 0x0053e677..0x0053e68c: FUN_005553b0(&get_position(), &DrawStateB, 0) into DrawStateA.
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), reinterpret_cast<float*>(&DrawStateB), nullptr);
    DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));
}

void FClientLootGameobj::update(float Dt)
{
    // FUN_0053e6a0 (RET 4): GameObject's update FUN_00539c60, then the owner tail on 0x803.
    FClientGameobj::update(Dt);
    GameobjOwnerUpdate(this, 0x803u, Dt);
}

void FClientLootGameobj::Slot198(DamageList const& Damage)
{
    // FUN_0053e770 (RET 4): multiplayer only.
    if (SinglePlayer())
        return;
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        // 0x0053e794..0x0053e7cb: the root entry -> the CLoot's vt+0x98 set_hit_pts.
        if (It->SubObjId == SUBOBJ_ID_ROOT)
        {
            static_cast<CSimple*>(NarrowObject(this, 0x803u))->set_hit_pts(It->HitPts);
            continue;
        }
        // 0x0053e7d0..0x0053e87f: any other entry. FCOMP 0.0 / TEST AH,0x41 / JNZ: only a value
        // above 0 falls through and becomes the contents' hit points (+0xf4); 0, below and NaN
        // cost a unit (+0xf0) and refill +0xf4 from the contents archetype's hit_pts (+0x1c).
        CLoot* const Loot = static_cast<CLoot*>(NarrowObject(this, 0x803u));
        if (It->HitPts > 0.0f)
        {
            Loot->ContentsHitPts = It->HitPts;
        }
        else
        {
            Loot->Units = Loot->Units - 1u;
            Loot->ContentsHitPts = Loot->ContentsArch->HitPts;
        }
    }
}

void FClientLootGameobj::Slot19c(int DestroyType)
{
    // LAB_0053e8a0 (RET 4).
    if (DestroyType != 2)
    {
        FClientSimpleGameobj::Slot19c(DestroyType);                            // FUN_00547500
        return;
    }
    // 0x0053e8ac..0x0053e8e4: tractored in -- no explosion: slot 87 with true; else
    // FUN_005361e0(TractoredExplosionArch, this, &get_position()) into ActionDB::add.
    if (TractoredExplosionArch == nullptr)
    {
        Slot15c(true);
        return;
    }
    Actions.add(Action_Explode::Create(TractoredExplosionArch, this, &get_position()));
}

FClientGameobj* ClientGameobj_CreateSolar()
{
    return new FClientSolarGameobj();
}


namespace
{
    // DAT_0066d33c / DAT_0066d340. The static initialiser LAB_0045a7e0 allocates the empty
    // list head and registers LAB_0045a820 with atexit, which destroys every node's watcher
    // (FUN_005154f0) and frees the head; a static std::list does both.
    std::list<BaseWatcher> G_GameobjWatchList;

    // The IObjRW a list watcher points at: its Watchable sub-object sits at IObjRW+8, and a
    // watcher with nothing behind it compares as null (0x0045aa6c..0045aa80).
    IObjRW const* WatchedGameobj(BaseWatcher const& Watcher)
    {
        Watchable const* const Target = Watcher.target();
        if (Target == nullptr)
            return nullptr;
        return reinterpret_cast<IObjRW const*>(reinterpret_cast<unsigned char const*>(Target) - 8);
    }

    // FUN_0045aa10(out, first, last, &Obj): the first node from first up to last whose
    // watched object equals Obj. Retail builds a temporary BaseWatcher on Obj for every node
    // and compares the two targets minus eight; comparing against Obj itself is the same test.
    std::list<BaseWatcher>::iterator FindWatchedGameobj(IObjRW const* Obj)
    {
        std::list<BaseWatcher>::iterator It = G_GameobjWatchList.begin();
        for (; It != G_GameobjWatchList.end(); ++It)
        {
            if (WatchedGameobj(*It) == Obj)
                break;
        }
        return It;
    }
}

void GameobjWatchListAdd(IObjRW* Obj)
{
    // FUN_0045a890. 0x0045a8bc: FUN_0045aa10 over the whole list; an existing entry ends it.
    // Otherwise 0x0045a8e5..0045a958: a watcher on Obj (+8 when Obj is non-null) is copied
    // into a new node linked in before the head (push_back; FUN_0045aad0 is that copy) and
    // DAT_0066d340 goes up by one.
    if (FindWatchedGameobj(Obj) != G_GameobjWatchList.end())
        return;
    // BaseWatcher(IObjRW*) is not a Common export, so the node is default-constructed and then
    // pointed at Obj+8 the way that constructor does it (set on an empty watcher is
    // set_pointer).
    G_GameobjWatchList.emplace_back();
    if (Obj != nullptr)
    {
        Watchable const* const Target =
            reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char const*>(Obj) + 8);
        G_GameobjWatchList.back().set(Target);
    }
}

void GameobjWatchListRemove(IObjRW* Obj)
{
    // FUN_0045a990. 0x0045a9a5: the same find; a hit is unlinked, its watcher released
    // (FUN_005154f0: set_pointer(0) when it watches something), the node freed and
    // DAT_0066d340 decremented.
    std::list<BaseWatcher>::iterator const It = FindWatchedGameobj(Obj);
    if (It == G_GameobjWatchList.end())
        return;
    G_GameobjWatchList.erase(It);
}

std::list<BaseWatcher> const& GameobjWatchList()
{
    return G_GameobjWatchList;
}

void ExplosionArchAddRef(Archetype::Explosion const* Expl)
{
    // FUN_00536100 (cdecl). 0x00536106: null explosion -> nothing. 0x0053610a..00536131: every
    // effect entry (retail std::list at +0x24; the port keeps a vector) -> FUN_0040f080 on the
    // entry's effect id, and a hit gets FUN_0040e960(1). 0x00536133..0053615d: every innards
    // debris id (vector +0x44..+0x48) -> Archetype::GetSimple, and a hit gets Root vt+0x0c
    // (slot 3, load).
    if (Expl == nullptr)
        return;
    for (size_t Index = 0; Index < Expl->Effects.size(); ++Index)
    {
        FEffectArch* const Fx = FindEffectArch(&Expl->Effects[Index].Fx.Id);
        if (Fx != nullptr)
            EffectArchAddRef(Fx, 1u);
    }
    for (size_t Index = 0; Index < Expl->InnardsDebrisObjects.size(); ++Index)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(Expl->InnardsDebrisObjects[Index]);
        if (Simple != nullptr)
            Simple->load();
    }
}

void ExplosionArchRelease(Archetype::Explosion const* Expl)
{
    // FUN_00536170 (cdecl). Same two walks as FUN_00536100: a found effect gets FUN_0040e990,
    // a found innards simple gets Archetype::Root::Release (0x005361c5, the Common import --
    // not a virtual call).
    if (Expl == nullptr)
        return;
    for (size_t Index = 0; Index < Expl->Effects.size(); ++Index)
    {
        FEffectArch* const Fx = FindEffectArch(&Expl->Effects[Index].Fx.Id);
        if (Fx != nullptr)
            EffectArchRelease(Fx);
    }
    for (size_t Index = 0; Index < Expl->InnardsDebrisObjects.size(); ++Index)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(Expl->InnardsDebrisObjects[Index]);
        if (Simple != nullptr)
            Simple->Release();
    }
}
