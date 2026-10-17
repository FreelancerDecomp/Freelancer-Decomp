// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cli_asteroid_med.h"

#include "cli_asteroid.h"
#include "client_explosion.h"   // Action_Explode (FUN_005361e0)
#include "client_objdb.h"       // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "common_server.h"      // IServer::SPBadLandsObjCollision (slot 54)
#include "common_misc.h"        // SinglePlayer
#include "dacom.h"              // FDUMP
#include "player_spawn.h"       // GetPlayerConnectionId (DAT_00673344)
#include "server_connect.h"     // GetLocalServerInterface (DAT_0067ecd0)

#include <cstdint>
#include <new>
#include <utility>

namespace
{
    // .rdata:005dc47c -- CliAsteroid.cpp's FDUMP file argument.
    char const kCliAsteroidSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp";

    static_assert(sizeof(FCliAsteroidCubeMap) <= sizeof(FCliAsteroid::MapAt08),
                  "the cube map must fit the retail 0x14 bytes at FCliAsteroid+0x08");
    static_assert(alignof(FCliAsteroidCubeMap) <= 8, "FCliAsteroid+0x08 is 8-aligned");
}

// ===========================================================================
// MedAsteroid
// ===========================================================================

FCliMedAsteroidGameobj::FCliMedAsteroidGameobj()
    : FClientGameobj(EClientGameobjKind::MedAsteroid)
{
    // 0x00524d1e..0x00524d34: FUN_005392d0(8) and the three vtable stores -- nothing else.
    // bWatchListed (+0xa8) is not written here; FUN_00524cb0 stores the instant-kill byte into
    // it after slot 92 (FUN_00539740) has already read it.
}

FCliMedAsteroidGameobj::~FCliMedAsteroidGameobj()
{
    // FUN_00524ec0: MedAsteroid adds no data -- the vtables go back and GameObject's
    // FUN_005394b0 runs (the base destructor).
}

bool FCliMedAsteroidGameobj::find(unsigned const& Id)
{
    // FUN_00524ee0 (RET 4). The exe imports the reference overload of CObject::Find and pushes
    // the slot's own reference argument (0x00524ee0 / 0x00524eea).
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    // 0x00524ef6..0x00524f02: advise only when the object was found.
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

void FCliMedAsteroidGameobj::Slot1a0(void const* Contact, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    // LAB_00524f20 (RET 0x10). 0x00524f20..0x00524f4d: bWatchListed, Other, Other's CBase
    // (+0xc, never null for a live Other) and its object, class & 0x503 == 0x503 (a CShip).
    if (bWatchListed != 0 && Other != nullptr && Other->GetObject() != nullptr &&
        (static_cast<unsigned>(Other->GetObject()->ObjectClass) & 0x503u) == 0x503u)
    {
        // 0x00524f4f..0x00524f5c: own slot 108 with the contact point (the first argument).
        Slot1b0(static_cast<Vector const*>(Contact));
        return;
    }
    // 0x00524f5f..0x00524f73: the arguments rewritten in place unchanged and a tail JMP to
    // GameObject's FUN_00539cd0.
    FClientGameobj::Slot1a0(Contact, B, Other, D);
}

void FCliMedAsteroidGameobj::Slot1ac(void const* A, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    // LAB_00524f80 (RET 0x10).
    if (bWatchListed == 0)
    {
        // 0x0052501a..0x00525030: GameObject's FUN_00539e70 with the same four arguments.
        FClientGameobj::Slot1ac(A, B, Other, D);
        return;
    }

    // 0x00524f95..0x00524fa2: Other's vt+0x20 get_id; 0 -> nothing.
    if (Other->get_id() == 0)
        return;

    // 0x00524fa8..0x00524fd7: in multiplayer only an Other the local player owns reports --
    // its class & 3 object's +0xb4 OwnerPlayer, or FUN_0054bb00 when the narrow fails, against
    // DAT_00673344.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = Other->GetObject();
        unsigned Owner = 0;
        if (OtherObj != nullptr && (static_cast<unsigned>(OtherObj->ObjectClass) & 3u) == 3u)
            Owner = static_cast<CSimple const*>(OtherObj)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }

    // 0x00524fd9..0x0052500c: {this id, Other's id, D's low word} and DAT_00673344 into
    // DAT_0067ecd0 slot 54 (+0xd8), no null test. The word at +0x0a is the stack's.
    SSPBadLandsObjCollisionInfo Info;
    Info.Field_0x00 = get_id();
    Info.ObjectId = Other->get_id();
    Info.Field_0x08 = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0a = 0;
    static_cast<IServer*>(GetLocalServerInterface())
        ->SPBadLandsObjCollision(Info, GetPlayerConnectionId());
}

void FCliMedAsteroidGameobj::Slot1b0(Vector const* Pos)
{
    // FUN_00525040 (RET 4). 0x00525044..0x00525051: nothing while an action is pending.
    if (Actions.get_action_count() != 0)
        return;

    if (ExplosionArch == nullptr)
    {
        // 0x005250c0..0x005250e2.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x7a5,
              "asteroid_mine told to explode but has no explosion archetype");
        return;
    }
    if (ExplosionArch->Process != 2)
    {
        // 0x00525064..0x0052508a.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x79c,
              "Explosion process for asteroid should be set to none");
        return;
    }
    // 0x0052508d..0x005250a3: FUN_005361e0(ExplosionArch, this, Pos or get_position()) -- the
    // pointer itself, no stack copy -- into the action list.
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Actions.add(Action_Explode::Create(ExplosionArch, this, Where));
}

unsigned short* FCliMedAsteroidGameobj::Slot1bc(unsigned short* Out, unsigned A, unsigned B,
                                               unsigned C, unsigned D)
{
    // LAB_005250f0.
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return CliAsteroidSlot1bcBody(Out);
}

unsigned short* CliAsteroidSlot1bcBody(unsigned short* Out)
{
    // LAB_005250f0: MOV EAX,[ESP+4]; MOV word [EAX],0; MOV word [EAX+2],0xffff; RET 0x14.
    Out[0] = 0;
    Out[1] = 0xffff;
    return Out;
}

// ===========================================================================
// The cube value
// ===========================================================================

FCliAsteroidCube::FCliAsteroidCube()
    : CubeId(0)
    , CubeSize(0xfa)
{
    // 0x0052219a..0x005221b7 (FUN_00522070's temporary): FUN_004b6800 builds the inner map
    // (the member's constructor here), +4 = 0, +0x38 = 0xfa. Cube, Orient and Pos are not
    // written.
}

FCliAsteroidCube::~FCliAsteroidCube()
{
    // FUN_00524af0 (thiscall on the value). 0x00524b12..0x00524b66: slot 87 (vt+0x15c) with
    // true on every asteroid in map order -- the ObjDB reap deletes them later.
    for (std::map<unsigned long, FCliMedAsteroidGameobj*>::iterator It = Asteroids.begin();
         It != Asteroids.end(); ++It)
    {
        It->second->Slot15c(true);
    }
    // 0x00524b68..0x00524c87: the map's erase(begin, end) and its destructor (the member's).
}

void FCliAsteroidCube::ApplyEvent(CmnAsteroid::asteroid_marker const* Event)
{
    // FUN_00524cb0 (RET 4). 0x00524cd2..0x00524ce0: find the event's asteroid id (+4).
    std::map<unsigned long, FCliMedAsteroidGameobj*>::iterator const It =
        Asteroids.find(Event->AsteroidId);

    if (Event->bCubeActive) // +0x0c
    {
        if (It != Asteroids.end())
        {
            // 0x00524d91..0x00524db3.
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x72d,
                  "asteroid cube told to create asteroid that already exists");
            return;
        }
        // 0x00524cff..0x00524d3b: operator new(0xac) and the inlined ctor.
        FCliMedAsteroidGameobj* const Med = new FCliMedAsteroidGameobj();
        // 0x00524d3f..0x00524d5e: FUN_0046d6d0 -- insert {AsteroidId, Med}.
        Asteroids.insert(std::pair<unsigned long const, FCliMedAsteroidGameobj*>(
            Event->AsteroidId, Med));
        // 0x00524d63..0x00524d75: vt+0x16c find(&Event->ObjId), then vt+0x170 (FUN_00539740).
        Med->find(Event->ObjId);
        Med->Slot170();
        // 0x00524d7b..0x00524d87: bWatchListed = bInstantKill (+0x0e), then FUN_00540c30(Med, 0).
        Med->bWatchListed = Event->bInstantKill ? 1 : 0;
        ClientObjDb_RegisterGameobj(Med, false);
        return;
    }

    if (It == Asteroids.end())
    {
        // 0x00524de0..0x00524e00.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x739,
              "asteroid cube told to delete asteroid that does not exist");
        return;
    }
    // 0x00524dba..0x00524dd3: slot 87 (vt+0x15c) with true on the gameobj, then erase
    // (FUN_0046c8c0).
    It->second->Slot15c(true);
    Asteroids.erase(It);
}

// ===========================================================================
// The field's cube map (FCliAsteroid +0x08)
// ===========================================================================

FCliAsteroidCubeMap& CliAsteroidCubeMap(FCliAsteroid* Field)
{
    return *reinterpret_cast<FCliAsteroidCubeMap*>(Field->MapAt08);
}

void CliAsteroidCubeMapConstruct(FCliAsteroid* Field)
{
    // FUN_005201e0 0x00520201..0x0052021a: the allocator bytes (+0x08, +0x09), the byte at
    // +0x14 and FUN_005274a0's head node -- one std::map here.
    new (Field->MapAt08) FCliAsteroidCubeMap();
}

void CliAsteroidCubeMapDestruct(FCliAsteroid* Field)
{
    // FUN_005205d0 0x005207f6..0x00520834: FUN_00527310 (erase(begin, end); each pair's
    // destructor FUN_00522480 runs the value's FUN_00524af0), then the head node and the
    // allocator block freed inline (FUN_00520500's body; FUN_00520500 itself is the EH unwind
    // copy, XREFs 0x005bff96 / 0x005bfff6).
    CliAsteroidCubeMap(Field).~FCliAsteroidCubeMap();
}
