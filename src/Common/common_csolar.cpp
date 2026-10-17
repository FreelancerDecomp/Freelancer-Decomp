// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CSolar (retail CSolar.cpp).
#include "common_cobject.h"
#include "common_cobject_internal.h" // CmnCobject::GetObjectDbCollisionGroup

#include "common_goods.h"
#include "common_id.h"
#include "common_physys.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_universe.h"
#include "common_misc.h"    // SinglePlayer()

#include "DALib.h"
#include "x86math.h" // I3DMathEngine::TransformMatrix (get_angular_velocity)

#include <list>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbCollisionGroup;

ID_String const& CSolar::get_loadout() const
{
    // Common.dll.c 0x3ad10. this+0x1b0 (ID_String), now a named field (see common_cobject.h).
    return Loadout;
}

// Listing .text:062b7320 .. .text:062b7411 (RET 4). The +0x1cc byte store at 062b739c is the
// list's allocator copy and the operator new(0xc) self-linked head at 062b73a2..062b73b5 is the
// list's own construction -- both are what std::list<FAnimScriptRef>'s default ctor does.
CSolar::CSolar(Class ObjectClass)
    : CEqObj(ObjectClass), bDestructible(false), bDynamic(false), AtmosphereRange(0.0f),
      DestSystem(0), GateAnimDirection(0), GateAnimState(4), GateAnimTimer(0.0f),
      PrevRing(0), NextRing(0), TradelaneSpaceNameId(0), VisitValue(0)
{
    // .text:062b734a..062b73cc: Nickname (+0x1a4), Loadout (+0x1b0), DestGate (+0x1b8) and
    // ParentNickname (+0x1e8) through ID_String::clear(). GateAnim starts at -1 (062b7387).
    Nickname.clear();
    Loadout.clear();
    DestGate.clear();
    ParentNickname.clear();
}

// Listing .text:0629a860 .. .text:0629abc8. Zero callers (dllexport-only). Retail inlines the
// CEqObj base copy; this port delegates it to CEqObj(CEqObj const&). Own fields: every scalar
// and ID_String is copied raw, GateAnim's handle dword too (0629aae7..0629aaf5, the
// FAnimScriptRef copy), and the +0x1cc list is copied element by element through push_back
// (FUN_0627f280 inserting before the new head, 0629ab55..0629ab70) -- std::list's copy ctor.
CSolar::CSolar(CSolar const& Other)
    : CEqObj(Other), Nickname(Other.Nickname), bDestructible(Other.bDestructible),
      bDynamic(Other.bDynamic),
      AtmosphereRange(Other.AtmosphereRange), Loadout(Other.Loadout), DestSystem(Other.DestSystem),
      DestGate(Other.DestGate), GateAnimDirection(Other.GateAnimDirection),
      GateAnim(Other.GateAnim), GateAnimState(Other.GateAnimState),
      GateAnimTimer(Other.GateAnimTimer), ContinualAnims(Other.ContinualAnims),
      PrevRing(Other.PrevRing), NextRing(Other.NextRing),
      TradelaneSpaceNameId(Other.TradelaneSpaceNameId), VisitValue(Other.VisitValue),
      ParentNickname(Other.ParentNickname)
{
}

// Listing .text:062b7420 .. .text:062b7550. The body proper is the flush_animations walk
// inlined (AnimDB::Rem of every live continual handle, then of GateAnim unconditionally, then
// CEqObj::flush_animations) and the vibe release. What follows in retail is member
// destruction: the +0x1cc list erased node by node through FUN_062b94c0 (each value's
// FAnimScriptRef destructor: StopPlayback + DestroyScript when != -1) and its head freed
// (062b74b3..062b74f6), then GateAnim's destructor inlined (062b74fd..062b752f), then
// ~CEqObj -- exactly the reverse declaration order of ContinualAnims / GateAnim below.
CSolar::~CSolar()
{
    // .text:062b7446..062b7479
    for (std::list<FAnimScriptRef>::const_iterator It = ContinualAnims.begin();
         It != ContinualAnims.end(); ++It)
    {
        if (It->Index != -1)
            AnimDB::Rem(It->Index);
    }
    AnimDB::Rem(GateAnim.Index);                                   // .text:062b747b
    CEqObj::flush_animations();                                    // .text:062b748c

    // .text:062b7491..062b74ab: TypeBits & 0x200 releases the object's vibe (CEqObj+0x104)
    // through a local copy.
    if ((TypeBits & 0x200) != 0)
    {
        int const Vibe = (int)VibeId;
        Reputation::Vibe::Free(Vibe);
    }
}

void CSolar::init(CreateParms const& Parms)
{
    // Common.dll.c 89065-89132 (0x57560). Every CSolar-specific field this reads lives
    // inside CEqObj::CreateParms's own storage (NicknameSrc at retail +0xb8, then the run of
    // fields from +0xbc to +0xe8 -- see the CreateParms struct comment).
    CEqObj::init(Parms);

    Nickname.Id = Parms.NicknameSrc.Id;

    bDestructible = Parms.bDestructible != 0;
    bDynamic = Parms.bDynamic != 0;

    // Static (non-dynamic) solars start with weapons offline outside single-player --
    // matches CEqObj::bWeaponsOffline's "IsDisabled gate" role (see its declaration comment).
    if (!SinglePlayer() && !bDynamic)
        bWeaponsOffline = true;

    DestSystem = Parms.DestSystem;
    DestGate.Id = Parms.DestGateId;
    PrevRing = Parms.PrevRing;
    NextRing = Parms.NextRing;
    TradelaneSpaceNameId = Parms.TradelaneSpaceNameId;
    AtmosphereRange = Parms.AtmosphereRange;
    VisitValue = Parms.VisitValue;
    ParentNickname.Id = Parms.ParentNicknameId;

    // .text:062b7602..062b7693: the gate's open animation, only for a solar with a destination
    // system whose archetype names one (Archetype::Solar +0xa0, first char tested without a
    // null test of the pointer). PlayScript (Anim vt+0x24) with the archetype's script index
    // (+0x44) and the engine instance (+0x4); the handle goes straight into GateAnim.
    if (DestSystem != 0)
    {
        Archetype::Solar* const SolarArch = (Archetype::Solar*)Archetype;
        char const* const OpenAnimName = SolarArch->OpenAnim.Value;
        if (OpenAnimName[0] != '\0')
        {
            GateAnim.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                                     AsOpaqueDword(this->EngineInstanceId),
                                                     OpenAnimName, nullptr, nullptr);
            if (GateAnim.Index == -1)
            {
                // FUN_062b7750(0x10000, 1) packs the severity 0x100001.
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0xe3, OpenAnimName,
                      Archetype->Nickname.get_string());
            }
        }
    }

    DockingBaseId = Parms.DockingCount;                            // .text:062b7696 (+0x160)

    unsigned const RawTypeBits = Parms.DockWithBaseId;
    DockingPointBaseId = RawTypeBits;                              // .text:062b769f (+0x164)

    // .text:062b76ab..062b76f9: diagnostic only, a dockable object with no base/system
    // destination.
    if ((TypeBits & 0x600d60) != 0 && (TypeBits & 2) == 0 &&
        (RawTypeBits & 0x3fffffff) == 0 && (DestSystem & 0x3fffffff) == 0)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: %s is dockable, but doesn't have a valid base or system to go to ",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x6d,
              Parms.NicknameSrc.get_string());
    }

    if ((TypeBits & 0x84) != 0)
        VisitValue = 1;

    init_docking_points(RawTypeBits);                              // .text:062b7715

    // .text:062b771a..062b7739: walk Parms' anim-name list (retail +0xe4, head at +0xe8),
    // one init_continual_anim per name. No null test: the list always has its head.
    for (std::list<char const*>::const_iterator It = Parms.ContinualAnims.begin();
         It != Parms.ContinualAnims.end(); ++It)
        init_continual_anim(*It);

    CSimple::update_zones(0.0f, 0);
}

void CSolar::init_continual_anim(char const* AnimName)
{
    // Listing .text:062b7d90 .. .text:062b7efd (RET 4).
    // .text:062b7daa..062b7e08: a local FAnimScriptRef (-1) is inserted at begin() of the
    // +0x1cc list (FUN_0627f280 with the head's next), then destroyed at once (its destructor
    // is inlined and does nothing for -1).
    {
        FAnimScriptRef Fresh;
        ContinualAnims.insert(ContinualAnims.begin(), Fresh);
    }

    // .text:062b7e0c..062b7e41: PlayScript (Anim vt+0x24) with the archetype's script index
    // (+0x44) and the engine instance (+0x4), stored into the new front element.
    FAnimScriptRef& Front = ContinualAnims.front();
    Front.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                          AsOpaqueDword(this->EngineInstanceId), AnimName,
                                          nullptr, nullptr);
    if (Front.Index == -1)
    {
        // .text:062b7e47..062b7e70: the front node is unlinked, its value destroyed through the
        // scalar-deleting destructor with flag 0 (FUN_062bb4d0, a no-op for -1), the node
        // freed and the size decremented -- pop_front().
        ContinualAnims.pop_front();
        // .text:062b7e73..062b7ea1: FDUMP(0x100001, ..., 0x194, name, archetype nickname).
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x194, AnimName,
              Archetype->Nickname.get_string());
        return;
    }

    // .text:062b7eba..062b7ede: EvalPlayback (Anim vt+0x2c) pushes nine dwords
    // (Anim, handle, 2, -2.0f, 1.0f, 0, 1.0f, 1.0f, 0); the two raw 0x3f800000 dwords are 1.0f.
    DALib::Anim->EvalPlayback(Front.Index, 2, -2.0f, 1.0f, nullptr,
                              AsOpaqueDword(0x3f800000),
                              AsOpaqueDword(0x3f800000), nullptr);
    AnimDB::Add(Front.Index);                                      // .text:062b7ee4
}

void CSolar::flush_animations()
{
    // Listing .text:062b7fa0 .. .text:062b7fde. AnimDB::Rem of every continual handle that is
    // not -1 (062b7fb0..062b7fc9), of GateAnim unconditionally (062b7fcb), then a tail JMP to
    // CEqObj::flush_animations. The list keeps its nodes.
    for (std::list<FAnimScriptRef>::const_iterator It = ContinualAnims.begin();
         It != ContinualAnims.end(); ++It)
    {
        if (It->Index != -1)
            AnimDB::Rem(It->Index);
    }

    AnimDB::Rem(GateAnim.Index);
    CEqObj::flush_animations();
}

void CSolar::cache_physical_props()
{
    // Common.dll.c 0x57930. CenterOfMass/Radius are the named CachedCenterOfMass (+0xd0) /
    // CachedRadius (+0xdc) members (see common_cobject.h) rather than raw pointer arithmetic.

    // 1. CSolar passes bIncludeChildren = true to include compound solar parts
    if (BodyPtr != nullptr)
    {
        CachedRadius = PhySys::GetRadiusR(this, true, CachedCenterOfMass);
        return;
    }

    // 2. Retail logs an error if solar lacks physical body, then delegates to CSimple base
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 200,
          "Missing ivp_real in CSimple::cache_physical_props()");
    CSimple::cache_physical_props();
}

float CSolar::get_mass() const
{
    // .text:0629ad00..0629ad09: FLD [archetype(+0x88, no null test) + 0x20] (Mass). Unlike
    // the generic CObject::get_mass, CSolar never consults the IVP body.
    return Archetype->Mass;
}

Vector CSolar::get_velocity() const
{
    // .text:063623d0: zero the three floats of the returned Vector (RET 4). ICF-folded
    // with CAsteroid::get_velocity.
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}

Archetype::Solar const* CSolar::solararch() const
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold).
    return (Archetype::Solar const*)Archetype;
}

Vector CSolar::get_angular_velocity() const
{
    // .text:0629ac70-0629acf8. Only a planetary solar (TypeBits & 7, the is_planetary test)
    // with a physics body (+0x54) reports a spin; everything else returns zero. The
    // object-space angular velocity (PhySys::GetAngularVelocityOS, 0629ac8d) is rotated to
    // world by I3DMathEngine +0x2c TransformMatrix(Out, Orientation, w) = Orientation * w
    // (0629acaa-0629acb7).
    if ((TypeBits & 7) != 0 && BodyPtr != nullptr)
    {
        Vector const ObjSpace = PhySys::GetAngularVelocityOS(this);
        FVector World;
        Acquire3DMathEngine()->TransformMatrix(World, (FMatrix const&)Orientation,
                                               (FVector const&)ObjSpace);
        return Vector{World.X, World.Y, World.Z};
    }
    return Vector{0.0f, 0.0f, 0.0f};
}

bool CSolar::is_planetary() const
{
    // Common.dll.c 0x3a7f0. Tests CSimple::TypeBits (inherited, +0xe0), not a CSolar-local flag.
    return (TypeBits & 7) != 0;
}

bool CSolar::is_system_gate() const
{
    // Common.dll.c 0x3a800
    return TypeBits == (TypeBits & 0xc40);
}

bool CSolar::is_waypoint() const
{
    // Common.dll.c 0x3a820
    return ((TypeBits >> 9) & 1) != 0;
}

bool CSolar::is_lane_ring() const
{
    // Common.dll.c 0x3a830
    return ((TypeBits >> 7) & 1) != 0;
}

bool CSolar::is_destructible() const
{
    // Common.dll.c 0x3a840. Raw byte at this+0x1a8 (bDestructible), not part of TypeBits.
    return bDestructible;
}

bool CSolar::is_dynamic() const
{
    // Common.dll.c 0x3a850
    return bDynamic;
}

float CSolar::get_atmosphere_range() const
{
    // Common.dll.c 0x3ad40
    return AtmosphereRange;
}

unsigned CSolar::get_prev_trade_ring() const
{
    // Common.dll.c 0x3ad50
    return PrevRing;
}

unsigned CSolar::get_next_trade_ring() const
{
    // Common.dll.c 0x3ad60
    return NextRing;
}

ID_String const& CSolar::GetParentNickname() const
{
    // Common.dll.c 0x54f00 (this+0x1e8 in retail; the previous 0x54bf0 xref pointed at
    // CShip::check_formation_follower_status, an unrelated function at a mis-derived address)
    return ParentNickname;
}

unsigned CSolar::get_dest_system() const
{
    // Common.dll.c 0x3ad20
    return DestSystem;
}

ID_String const& CSolar::get_dest_gate() const
{
    // Common.dll.c 0x3ad30
    return DestGate;
}

unsigned char CSolar::GetVisitValue() const
{
    return VisitValue;
}

void CSolar::SetVisitValue(unsigned char NewValue)
{
    // Common.dll.c 0x58a60
    VisitValue = NewValue;
}

unsigned int CSolar::get_tradelane_space_name() const
{
    // Common.dll.c 0x58a70. A separate field from ParentNickname (this+0x1e0 vs +0x1e8 in
    // retail) despite the similar names -- CSolar::init (0x57560) loads them from different
    // CreateParms offsets. Previously read as ParentNickname.Id by mistake.
    return TradelaneSpaceNameId;
}

void CSolar::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // Listing .text:062b7780 .. .text:062b791e (RET 8). Velocity is never read (only ESI =
    // this and EDI = AngularVelocity are loaded).
    (void)Velocity;

    // .text:062b7786..062b77f2: every PhySys::CreateParms field retail seeds (the DEFAULT_*
    // damping globals, system 0, type 2, the flag bytes, Name[0], inertia flag and vector)
    // is overwritten below or is zero; the port starts from zero for the bytes retail leaves
    // uninitialized. Name = _itoa(Id, Name, 0x18) -- radix 24, as pushed.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.bEnableCollisions = 1;                                     // .text:062b77d9
    _itoa((int)Id, Phy.Name, 0x18);

    // .text:062b77ff..062b782b: (x*x + y*y) + z*z kept on the x87 stack (double here), FCOMP
    // 0.0f, TEST AH,0x41 + JNZ to 062b7847 when not greater (zero, negative or unordered).
    double const SpinSq =
        ((double)AngularVelocity.x * AngularVelocity.x +
         (double)AngularVelocity.y * AngularVelocity.y) +
        (double)AngularVelocity.z * AngularVelocity.z;
    if (!(SpinSq > 0.0))
    {
        // .text:062b7847..062b7864: flag 1, all four dampings 0x49742400 = 1.0e6f.
        Phy.bFlag1c = 1;
        Phy.LinearDamping = 1.0e6f;
        Phy.AngularDamping[0] = 1.0e6f;
        Phy.AngularDamping[1] = 1.0e6f;
        Phy.AngularDamping[2] = 1.0e6f;
    }
    else
    {
        // .text:062b782d..062b7841: flag 0, linear 0x5368d4a5 = 1.0e12f, angular 0.
        Phy.bFlag1c = 0;
        Phy.LinearDamping = 1.0e12f;
        Phy.AngularDamping[0] = 0.0f;
        Phy.AngularDamping[1] = 0.0f;
        Phy.AngularDamping[2] = 0.0f;
    }

    // .text:062b786c..062b7892: mass = archetype +0x20, type 1, system id from the object
    // database. The archetype is used without a null test.
    Archetype::Solar const* const SolarArch = (Archetype::Solar const*)Archetype;
    Phy.Mass = Archetype->Mass;
    Phy.Type = 1;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);

    // .text:062b789c..062b78c3: _splitpath(arch +0xc, 0, 0, 0, Ext); _stricmp(Ext, ".sph").
    char Ext[256];
    _splitpath(SolarArch->DaArchetype.Value, nullptr, nullptr, nullptr, Ext);
    if (_stricmp(Ext, ".sph") == 0)
    {
        // .text:062b78c5..062b78ed: radius = arch +0x98; FCOMP 0.0f, TEST AH,0x41 + JNZ skips
        // the build when not greater.
        float const Radius = SolarArch->SolarRadius;
        if (Radius > 0.0f)
            PhySys::BuildIVP_Sphere(this, Radius, Phy);
    }
    else
    {
        PhySys::BuildIVP_Real(this, Phy);                          // .text:062b78fd
    }

    // .text:062b7905..062b790d: only a spinning solar (flag 0) gets its spin.
    if (Phy.bFlag1c == 0)
        PhySys::AddToAngularVelocityOS(this, AngularVelocity);
}

void CSolar::configure_system_gate(char const* AnimName)
{
    // Listing .text:062b7a00 .. .text:062b7a6c (RET 4). The gate-animation start CSolar::init
    // inlines, as its own function: an empty name does nothing (the first char is read
    // without a null test of the pointer); otherwise PlayScript (Anim vt+0x24) with the
    // archetype's script index (+0x44) and the engine instance (+0x4) into GateAnim, and
    // FDUMP(0x100001, ..., 0xe3, name, archetype nickname) when it returns -1.
    if (AnimName[0] == '\0')
        return;

    GateAnim.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                             AsOpaqueDword(this->EngineInstanceId), AnimName,
                                             nullptr, nullptr);
    if (GateAnim.Index == -1)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0xe3, AnimName,
              Archetype->Nickname.get_string());
    }
}

int CSolar::update(float Dt, unsigned Flags)
{
    // Common.dll.c 89296-89305 (0x57a70). `TypeBits == (TypeBits & 0xc40)` is exactly
    // is_system_gate()'s own test; delegate to it for readability.
    CEqObj::update(Dt, Flags);
    if (is_system_gate())
        update_system_gate(Dt);
    return 0;
}

void CSolar::update_system_gate(float Dt)
{
    // Listing .text:062b7ab0 .. .text:062b7c8b (RET 4), two jump tables (062b7c90 for a
    // non-zero GateAnimDirection, 062b7ca4 for zero). Every notification goes through the
    // virtual CEqObj::notify (vt+0xc8, CSolar's slot 0639d48c) as notify(1, &Code), Code an
    // int written into the Dt argument slot.
    int const State = GateAnimState;

    // .text:062b7ab4..062b7ae9: the timer runs up in state 0 (Dt + Timer), down in state 2
    // (Timer - Dt), and is zeroed in every other state.
    if (State == 0)
        GateAnimTimer = Dt + GateAnimTimer;
    else if (State == 2)
        GateAnimTimer = GateAnimTimer - Dt;
    else
        GateAnimTimer = 0.0f;

    int Code;
    if (GateAnimDirection != 0)                                    // .text:062b7aef
    {
        switch (State)
        {
        case 3:
        case 4:
            // .text:062b7b0b..062b7b26: Code 0 first, then the state and timer reset.
            Code = 0;
            notify((INotify::Event)1, &Code);
            GateAnimState = 0;
            GateAnimTimer = 0.0f;
            return;

        case 0:
            // .text:062b7b31..062b7b42: FCOMP 10.0f (DAT_0639e884), TEST AH,0x41 + JNZ to
            // 062b7b5b when Timer is not greater (less, equal or unordered); a greater timer
            // falls into the case-2 code.
            if (!(GateAnimTimer > 10.0f))
            {
                // .text:062b7b5b..062b7b97
                if (GateAnim.Index == -1)
                    return;
                DALib::Anim->EvalPlayback(GateAnim.Index, 0, -2.0f, 1.0f, nullptr,
                                          AsOpaqueDword(0x3f800000),
                                          AsOpaqueDword(0x3f800000), nullptr);
                AnimDB::Add(GateAnim.Index);
                return;
            }
            [[fallthrough]];
        case 2:
            // .text:062b7b44..062b7b56 -> 062b7c78: state 1 first, then Code 1.
            GateAnimState = 1;
            Code = 1;
            notify((INotify::Event)1, &Code);
            return;

        default:                                                   // case 1, above 4
            return;
        }
    }

    switch (State)                                                 // .text:062b7ba4
    {
    case 0:
    case 1:
        // .text:062b7bb4..062b7bbe: 0x40a00000 = 5.0f.
        GateAnimState = 2;
        GateAnimTimer = 5.0f;
        return;

    case 2:
        // .text:062b7bcd..062b7bde: FCOMP 0.0f (DAT_06399408), TEST AH,0x41 + JP out when the
        // parity of C0|C3 is even -- greater (neither) or unordered (both); less or equal
        // goes on.
        if (!(GateAnimTimer <= 0.0f))
            return;
        if (GateAnim.Index != -1)                                  // .text:062b7be4
        {
            DALib::Anim->EvalPlayback(GateAnim.Index, 1, -2.0f, 1.0f, nullptr,
                                      AsOpaqueDword(0x3f800000),
                                      AsOpaqueDword(0x3f800000), nullptr);
            AnimDB::Add(GateAnim.Index);
        }
        // .text:062b7c25..062b7c3e: Code 2 first, then state 3.
        Code = 2;
        notify((INotify::Event)1, &Code);
        GateAnimState = 3;
        return;

    case 3:
        // .text:062b7c4d..062b7c83: Anim vt+0x44 on the handle; zero keeps waiting, otherwise
        // state 4 first, then Code 3.
        if (DALib::Anim->FilterObjects((void*)GateAnim.Index) == 0)
            return;
        GateAnimState = 4;
        Code = 3;
        notify((INotify::Event)1, &Code);
        return;

    default:                                                       // above 3
        return;
    }
}

ObjActivateResult CSolar::activate(bool bOn, unsigned DockIndex)
{
    // Common.dll.c 89397-89421 (0x57cc0).
    if (is_system_gate())
    {
        if (DockIndex != 0)                                        // .text:062b7cd5..062b7cfd
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp",
                  0x15c, "UHH index not 0");
        }
        unsigned char const OldDirection = GateAnimDirection;
        GateAnimDirection = bOn ? 1 : 0;
        // .text:062b7d0c..062b7d1f: `(new == old) + 1` -- 2 (ObjActivateNoChange) when the
        // byte already held the value, 1 (ObjActivateChanged) otherwise. Same mapping as
        // CEqObj::activate.
        return (GateAnimDirection == OldDirection) ? ObjActivateNoChange : ObjActivateChanged;
    }
    // .text:062b7d2a: lane rings return 1.
    if (is_lane_ring())
        return ObjActivateChanged;
    return CEqObj::activate(bOn, DockIndex);
}

bool CSolar::get_activate_state(std::vector<bool>& OutStates)
{
    // Common.dll.c 89428-89446 (0x57d50). Retail hand-inlines a VC6 vector<bool>::push_back
    // via FUN_062bac30; CEqObj::get_activate_state above already replaces the equivalent
    // hand-inlined push with std::vector<bool>::push_back, so this does the same.
    if (is_system_gate())
    {
        OutStates.push_back(GateAnimDirection != 0);
        return GateAnimDirection != 0;
    }
    if (is_lane_ring())
        return false;
    return CEqObj::get_activate_state(OutStates);
}

CEquip* CSolar::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch, bool bMissionFlag)
{
    // Common.dll.c 89513-89549 (0x57f00). Retail hand-builds a raw CInternalEquip and
    // stamps CETradeLaneEquip's vtable on afterward -- a compiler fold of what is, for the
    // port's purposes, just constructing CETradeLaneEquip normally. Only CSolar (jump
    // gates/holes) special-cases trade-lane equipment this way; everything else falls
    // through to the shared CEqObj::alloc_equip.
    // .text:062b7f1d-062b7f26: Arch vt+0x10 get_class_type, no null test.
    if (Arch->get_class_type() == Archetype::AClassTradeLane)
    {
        // 062b7f2f..062b7f6f: operator new(0x20) (CEquip's, malloc); a null block skips the
        // constructor and falls through to CEqObj::alloc_equip below.
        CETradeLaneEquip* const Equip = new CETradeLaneEquip(
            this, EquipId, (Archetype::TradeLaneEquip const*)Arch, bMissionFlag);
        if (Equip != nullptr)
            return Equip;
    }
    return CEqObj::alloc_equip(EquipId, Arch, bMissionFlag);
}

namespace
{
    // .text:062b8839..062b89e4, inlined in CSolar::ReadObj: the VC6 vector<EquipDesc>
    // assignment `Parms.Equipment = Map->Items`. Retail's own self-assignment test
    // (062b8842 `CMP EDI,EBP; JZ`) compares two distinct objects here and never fires.
    // EquipDesc is a 0x20-byte POD: the overwrite loops are `MOVSD.REP` x8 and the
    // fill loops call FUN_0626c1e0 (copy-construct into raw storage).
    void AssignEquipDescsFromLoadout(EquipDescVector& Dst, std::vector<EquipDesc> const& Src)
    {
        EquipDesc const* const SrcBegin = Src.data();
        EquipDesc const* const SrcEnd = SrcBegin + Src.size();
        unsigned const SrcCount = (unsigned)Src.size();
        unsigned const DstSize = (Dst.Begin != nullptr) ? (unsigned)(Dst.End - Dst.Begin) : 0;

        if (SrcCount <= DstSize)                                       // 062b8874 JA (unsigned)
        {
            // 062b8878..062b88c2: overwrite the first SrcCount slots, End = Begin + SrcCount.
            EquipDesc* Out = Dst.Begin;
            for (EquipDesc const* In = SrcBegin; In != SrcEnd; ++In, ++Out)
                *Out = *In;
            Dst.End = Dst.Begin + SrcCount;
            return;
        }

        unsigned const DstCapacity = (Dst.Begin != nullptr) ? (unsigned)(Dst.Cap - Dst.Begin) : 0;
        if (SrcCount <= DstCapacity)                                   // 062b88e7 JA (unsigned)
        {
            // 062b88ef..062b8974: overwrite the DstSize live slots, copy-construct the rest
            // after the old End, End = Begin + SrcCount.
            EquipDesc const* const SrcMid = SrcBegin + DstSize;
            EquipDesc* Out = Dst.Begin;
            for (EquipDesc const* In = SrcBegin; In != SrcMid; ++In, ++Out)
                *Out = *In;
            Out = Dst.End;
            for (EquipDesc const* In = SrcMid; In != SrcEnd; ++In, ++Out)
                new (Out) EquipDesc(*In);
            Dst.End = Dst.Begin + SrcCount;
            return;
        }

        // 062b8979..062b89e4: operator delete(Begin), operator new(SrcCount * 0x20) (a
        // negative count clamps to 0 at 062b8997, unreachable for a real vector),
        // copy-construct every element, End = Cap = the end of the copy.
        ::operator delete(Dst.Begin);
        EquipDesc* const NewBuf = (EquipDesc*)::operator new(SrcCount * sizeof(EquipDesc));
        Dst.Begin = NewBuf;
        EquipDesc* Out = NewBuf;
        for (EquipDesc const* In = SrcBegin; In != SrcEnd; ++In, ++Out)
            new (Out) EquipDesc(*In);
        Dst.End = Out;
        Dst.Cap = Out;
    }
}

bool CSolar::ReadObj(INI_Reader& Reader, Archetype::Solar*& OutArch, CreateParms& Parms)
{
    // .text:062b7ff0..062b8a41. Static INI loader for one [Object] section of a system file:
    // parses every recognized key into Parms, resolves OutArch from "archetype", then
    // validates and finishes. Retail calls [FDUMP] unconditionally at every diagnostic.
    // 062b8001..062b8008: retail snapshots the reader state into a local it never reads
    // again (local_c08 has no other reference) and never restores it.
    INI_Reader::State SavedState;
    Reader.get_state(SavedState);

    OutArch = nullptr;                                                 // 062b8018
    ID_String LoadoutId;                                               // [ESP+0x14]
    LoadoutId.clear();
    bool bDestructibleSet = false;                                     // [ESP+0x12]
    bool bAtmosphereSet = false;                                       // [ESP+0x13]

    while (Reader.read_value())
    {
        if (Reader.is_value("archetype"))
        {
            ID_String ArchName;
            ArchName.Id = CreateID(Reader.get_value_string());
            // Retail literally calls GetAsteroidID here, not GetSolarID -- both are
            // ICF-folded to the same "return Name.Id" body in retail, so this is faithful
            // to the actual call graph rather than the more semantically-obvious sibling.
            unsigned const ArchId = Archetype::GetAsteroidID(ArchName);
            OutArch = Archetype::GetSolar(ArchId);
            if (OutArch == nullptr)
            {
                // 062b8700..062b8742: warning, return false.
                char const* const NickStr = Parms.NicknameSrc.get_string();
                char const* const ArchStr = ArchName.get_string();
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Archetype \"%s\" not found while reading Solar object \"%s\"\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x1da, ArchStr, NickStr);
                return false;
            }

            // .text:062b808e-062b8097: the list at Parms+0xac goes straight to the archetype,
            // which clears it and appends one full-health desc per collision group.
            OutArch->get_undamaged_collision_group_list(Parms.ArchGroupDescs);
        }
        else if (Reader.is_value("nickname"))
        {
            unsigned const NickId = CreateID(Reader.get_value_string());
            Parms.NicknameSrc.Id = NickId;
            Parms.Id = NickId;
        }
        else if (Reader.is_value("pos"))
        {
            Parms.Pos.x = Reader.get_value_float(0);
            Parms.Pos.y = Reader.get_value_float(1);
            Parms.Pos.z = Reader.get_value_float(2);
        }
        else if (Reader.is_value("rotate"))
        {
            Vector RotateDegrees;
            RotateDegrees.x = Reader.get_value_float(0);
            RotateDegrees.y = Reader.get_value_float(1);
            RotateDegrees.z = Reader.get_value_float(2);
            Parms.Orient = EulerMatrix(RotateDegrees);
        }
        else if (Reader.is_value("spin"))
        {
            Parms.AngularVelocity.x = Reader.get_value_float(0);
            Parms.AngularVelocity.y = Reader.get_value_float(1);
            Parms.AngularVelocity.z = Reader.get_value_float(2);
        }
        else if (Reader.is_value("visit"))
        {
            Parms.VisitValue = (unsigned char)Reader.get_value_int(0);
        }
        else if (Reader.is_value("parent"))
        {
            Parms.ParentNicknameId = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("goto"))
        {
            unsigned const SystemId = Universe::get_system_id(Reader.get_value_string(0));
            Parms.DestSystem = SystemId;
            if ((SystemId & 0x3fffffff) == 0)                          // 062b8249 TEST; JNZ skips
            {
                // 062b8256..062b827d: the message re-reads value 0.
                FDUMP(0x100001, "%s(%d) : *** ERROR: Jumpgate has invalid destination: %s\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x218, Reader.get_value_string(0));
            }
            Parms.DestGateId = CreateID(Reader.get_value_string(1));
        }
        else if (Reader.is_value("base"))
        {
            char const* BaseName = Reader.get_value_string();
            unsigned const BaseId = Universe::get_base_id(BaseName);
            Parms.DockingCount = BaseId;                               // retail +0x5c
            if (BaseId == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find base %s for solar object.",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x224, BaseName);
            }
        }
        else if (Reader.is_value("dock_with"))
        {
            char const* BaseName = Reader.get_value_string();
            unsigned const BaseId = Universe::get_base_id(BaseName);
            Parms.DockWithBaseId = BaseId;
            if (BaseId == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find docking target %s for solar object.",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x22c, BaseName);
            }
        }
        else if (Reader.is_value("loadout"))
        {
            LoadoutId.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("atmosphere_range"))
        {
            // Retail +0xdc of the inherited CEqObj::CreateParms (see CSolar::init).
            Parms.AtmosphereRange = Reader.get_value_float(0);
            bAtmosphereSet = true;
        }
        else if (Reader.is_value("anim"))
        {
            // .text:062b839f..062b83e8: StringAlloc(value, false), then push_back onto
            // Parms' anim-name list (retail +0xe4: node via FUN_062b9490 before the head at
            // +0xe8, ++size at +0xec). The list is the caller's; no lazy allocation.
            char const* AnimName = StringAlloc(Reader.get_value_string(), false);
            Parms.ContinualAnims.push_back(AnimName);
        }
        else if (Reader.is_value("prev_ring"))
        {
            Parms.PrevRing = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("next_ring"))
        {
            Parms.NextRing = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("tradelane_space_name"))
        {
            Parms.TradelaneSpaceNameId = (unsigned)Reader.get_value_int(0);
        }
        else if (Reader.is_value("destructible"))
        {
            bDestructibleSet = true;
            Parms.bDestructible = Reader.get_value_bool(0) ? 1 : 0;
        }
        else if (Reader.is_value("space_costume"))
        {
            // Common.dll.c 89812-89877: Parms+0x64..+0x97, the space costume.
            CostumeDescriptions const* Costumes = GetCostumeDescriptions();
            Costume* CostumeOut = &Parms.SpaceCostume;
            // 062b84a4..062b8507: head (bodypart kind 1) from value 0.
            if (!Reader.is_value_empty(0) && Reader.get_value_string(0)[0] != '\0')
            {
                bodypart const* Head = Costumes->find_bodypart(1, Reader.get_value_string(0));
                if (Head != nullptr)
                {
                    CostumeOut->PartIds[0] = Head->Id;                 // +0x64
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: space costume head %s does not exist\n",
                          "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x25e, Reader.get_value_string(0));
                }
            }
            // 062b850a..062b856a: body (bodypart kind 0) from value 1.
            if (!Reader.is_value_empty(1) && Reader.get_value_string(1)[0] != '\0')
            {
                bodypart const* Body = Costumes->find_bodypart(0, Reader.get_value_string(1));
                if (Body != nullptr)
                {
                    CostumeOut->PartIds[1] = Body->Id;                 // +0x68
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: space costume body %s does not exist\n",
                          "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x26f, Reader.get_value_string(1));
                }
            }
            // 062b856d..062b85b8: every further value is an accessory. Retail has neither a
            // null test on find_accessory's result (062b859b `MOV EDX,[EAX]`: an unknown
            // nickname dereferences null) nor a bound on the count: it loads the count
            // (+0x94), stores the id at +0x74 + count*4, then reloads the count and stores
            // count+1. A ninth accessory therefore lands on the count itself (+0x94) -- very
            // strange, reproduced as retail by indexing the accessory slots unbounded.
            unsigned* const AccessorySlots = CostumeOut->AccessoryIds;   // +0x74
            unsigned Index = 2;
            while (!Reader.is_value_empty(Index))
            {
                accessory const* const Acc = Costumes->find_accessory(Reader.get_value_string(Index));
                ++Index;
                unsigned const Slot = CostumeOut->AccessoryCount;
                AccessorySlots[Slot] = Acc->Id;
                CostumeOut->AccessoryCount = CostumeOut->AccessoryCount + 1;
            }
            // 062b85ba..062b85e5: no body -> "malformed" with the whole value line.
            if (CostumeOut->PartIds[1] == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: space costume malformed %s",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x27c, Reader.get_value_string());
            }
        }
        else if (Reader.is_value("voice"))
        {
            // Common.dll.c 89881-89884 (Parms+0x98).
            Parms.VoiceId = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("comm_appr"))
        {
            CostumeDescriptions const* Costumes = GetCostumeDescriptions();
            Costumes->get_costume(Reader.get_value_string(), Parms.SpaceCostume);
        }
    }

    // 062b8649..062b86ac: only when "atmosphere_range" was read. A null archetype goes
    // straight to the missing-archetype error (062b865c). `FLD Solar+0x98; FCOMP Parms+0xdc;
    // TEST AH,0x41; JNZ` skips when radius <= atmosphere (or unordered); the error is the
    // fall-through, radius > atmosphere.
    if (bAtmosphereSet && OutArch != nullptr && Parms.AtmosphereRange < OutArch->SolarRadius)
    {
        // Arguments in retail's push order: file name, (int)atmosphere (ftol truncates),
        // archetype nickname.
        char const* const FileName = Reader.get_file_name();
        int const AtmosphereInt = (int)Parms.AtmosphereRange;
        char const* const ArchStr = OutArch->Nickname.get_string();
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Solar with Archetype(%s) is larger than atmosphere %df in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x289, ArchStr, AtmosphereInt, FileName);
    }

    if (OutArch == nullptr)                                            // 062b86af / 062b86b8
    {
        char const* const FileName = Reader.get_file_name();
        char const* const NickStr = Parms.NicknameSrc.get_string();
        FDUMP(0x100001, "%s(%d) : *** ERROR: Solar with Nickname(%s) missing archetype in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x291, NickStr, FileName);
        return false;
    }

    if (Parms.NicknameSrc.IsEmpty())                                   // 062b8743..062b8799
    {
        char const* const FileName = Reader.get_file_name();
        char const* const ArchStr = OutArch->Nickname.get_string();
        FDUMP(0x100001, "%s(%d) : *** ERROR: Solar with Archetype(%s) missing nickname in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x296, ArchStr, FileName);
        return false;
    }

    // 062b879a..062b87ea: +0x60 = Verify(nickname) ? nickname : -1; the error test is on the
    // stored value (`CMP EDI,-1`), not on Verify's result.
    unsigned const VibeId = Reputation::Vibe::Verify((int)Parms.NicknameSrc.Id)
        ? Parms.NicknameSrc.Id : (unsigned)-1;
    Parms.VibeId = VibeId;
    if (VibeId == (unsigned)-1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to read reputation information for solar(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x2a3, Parms.NicknameSrc.get_string());
        return false;
    }

    // 062b87eb..062b8807: no "loadout" key -> the archetype's (Solar+0xa8).
    if (LoadoutId.IsEmpty())
        LoadoutId.Id = OutArch->Loadout.Id;
    // 062b880b..062b8822: IsEmpty, then a raw id test of its own.
    if (!LoadoutId.IsEmpty() && LoadoutId.Id != 0)
    {
        Loadout::Map const* const LoadoutMap = Loadout::Get(LoadoutId.Id);
        if (LoadoutMap != nullptr)
        {
            AssignEquipDescsFromLoadout(Parms.Equipment, LoadoutMap->Items);
        }
        else
        {
            // 062b89e6..062b8a15: reported, not fatal.
            char const* const LoadoutStr = LoadoutId.get_string();
            char const* const NickStr = Parms.NicknameSrc.get_string();
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s: Unable to find [Loadout] %s.",
                  "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x2c9, NickStr, LoadoutStr);
        }
    }

    // 062b8a18..062b8a2f: no "destructible" key -> the archetype's byte (Solar+0x94).
    if (!bDestructibleSet)
        Parms.bDestructible = (unsigned char)OutArch->bDestructible;

    return true;
}

float CSolar::get_atmosphere_immersion(Vector const& Pos) const
{
    // Common.dll.c 0x57990 (L89250-89264): if AtmosphereRange > 0 and Pos is within
    // AtmosphereRange of this object's own world position (squared-distance compare),
    // the position is immersed in the atmosphere -> 1.0f, else 0.0f. Field mapping
    // confirmed from the decomp offsets: this+0x2c/0x30/0x34 == WorldPosition.x/y/z
    // (EngineObject+0x2c, see common_cobject.h). Previously stubbed to always return
    // 0.0f -- fixed.
    // .text:062b7990-062b79f2. FCOMP 0.0f; TEST AH,0x41; JNZ -> 0.0f for less, equal and
    // unordered.
    if (!(AtmosphereRange > 0.0f))
        return 0.0f;

    // 062b79a7-062b79d2: the deltas to this+0x2c/0x30/0x34 stay on the x87 stack; the sum is
    // (Dx*Dx + Dz*Dz) + Dy*Dy and R*R is unrounded. FCOMPP R*R against the sum; TEST
    // AH,0x41; JNZ -> 0.0f unless R*R is greater.
    Vector const& WorldPos = get_position();
    double const Dx = (double)Pos.x - WorldPos.x;
    double const Dy = (double)Pos.y - WorldPos.y;
    double const Dz = (double)Pos.z - WorldPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    double const RangeSq = (double)AtmosphereRange * AtmosphereRange;
    if (RangeSq > DistSq)
        return 1.0f;
    return 0.0f;
}
