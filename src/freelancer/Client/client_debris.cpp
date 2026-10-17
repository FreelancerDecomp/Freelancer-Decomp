// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_debris.h"

#include "client_explosion.h"   // Action_Explode (FUN_005361e0)
#include "client_objdb.h"       // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "common_csys.h"        // LookMatrix, Acquire3DMathEngine
#include "common_ini.h"         // INI_Reader
#include "common_physys.h"
#include "common_universe.h"    // Archetype::GetExplosion
#include "dacom.h"              // FDUMP
#include "gf_object_ship.h"     // ShipTransposeRotate (FUN_00447be0)
#include "particle_effect.h"    // CliCreateEffectOneShot (FUN_00503070)
#include "star_sys.h"           // StarSysGetActiveSystemId / StarSysFindNebula
#include "vis_effect_arch.h"    // FindEffectArch (FUN_0040f080)
#include "x86math.h"            // I3DMathEngine, FTransform

#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

namespace
{
    char const kDebrisSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Debris.cpp";

    // DAT_005caeec: 1/32768, the rand() scale.
    constexpr float kRandScale = 3.0517578125e-05f;

    // DAT_006754f4 -- the [Debris] table, keyed by the nickname id (the static initialiser
    // LAB_00530860 builds it, LAB_00530890 tears it down at exit).
    std::map<unsigned, FDebrisArch> G_DebrisArchDb;

    // DAT_006754d0 -- the record FUN_00530a10 hands out when a name is unknown (LAB_00530960).
    FDebrisArch G_DefaultDebrisArch;

    // DAT_00675514 -- Debris.cpp's lazily acquired 3DMathEngine (0x00530372..0x00530381).
    I3DMathEngine* G_DebrisMathEngine = nullptr;

    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise -- the `this`,
    // `this + 0xc` and object tests 0x0053077c..0x00530799 inlines.
    CObject* NarrowDebrisObject(FClientGameobj* Gameobj, unsigned Mask)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }
}

// ===========================================================================
// The [Debris] table
// ===========================================================================

FDebrisArch::FDebrisArch()
{
    LifetimeMin = 0.0f;
    LifetimeMax = FLT_MAX; // 0x7f7fffff
    DeathMethod = 1;
    Explosion.clear();
    Trail.clear();
    LinearDrag = 0.0f;
    AngularDrag.x = 0.0f;
    AngularDrag.y = 0.0f;
    AngularDrag.z = 0.0f;
}

bool ReadDebrisArch(INI_Reader* Reader, bool bQuiet)
{
    // FUN_00530a20 (cdecl).
    FDebrisArch Arch;
    ID_String Nickname;
    Nickname.clear();

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("trail"))
        {
            Arch.Trail.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("lifetime"))
        {
            // 0x00530aec..0x00530b43: one value -> min = max; with a second, max is that value
            // unless it is below min (TEST AH,0x5 / JP), then max = min. The second value is
            // read again for the store.
            Arch.LifetimeMin = Reader->get_value_float(0);
            if (Reader->is_value_empty(1))
                Arch.LifetimeMax = Arch.LifetimeMin;
            else if (Reader->get_value_float(1) < Arch.LifetimeMin)
                Arch.LifetimeMax = Arch.LifetimeMin;
            else
                Arch.LifetimeMax = Reader->get_value_float(1);
        }
        else if (Reader->is_value("explosion"))
        {
            Arch.Explosion.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("death_method"))
        {
            // 0x00530b7f..0x00530c0d: the value copied into a 16-byte buffer (at most 15
            // characters; none when the string is null) and compared without case. The cut
            // cannot turn a longer value into either name, so comparing the value itself
            // decides the same; any other value leaves the method as it was.
            char const* const Method = Reader->get_value_string();
            if (Method != nullptr)
            {
                if (_stricmp(Method, "exploding") == 0)
                    Arch.DeathMethod = 0;
                else if (_stricmp(Method, "vanishing") == 0)
                    Arch.DeathMethod = 1;
            }
        }
        else if (Reader->is_value("linear_drag"))
        {
            Arch.LinearDrag = Reader->get_value_float(0);
        }
        else if (Reader->is_value("angular_drag"))
        {
            // 0x00530c35..0x00530c7b: read as values 2, 1, 0.
            float const Z = Reader->get_value_float(2);
            float const Y = Reader->get_value_float(1);
            float const X = Reader->get_value_float(0);
            Arch.AngularDrag.x = X;
            Arch.AngularDrag.y = Y;
            Arch.AngularDrag.z = Z;
        }
    }

    // 0x00530c90..0x00530d0c: a section without a nickname is dropped; an existing nickname
    // keeps its first record (FUN_005310c0 does not replace) and warns unless bQuiet.
    if (Nickname.IsEmpty())
        return false;
    std::pair<std::map<unsigned, FDebrisArch>::iterator, bool> const Inserted =
        G_DebrisArchDb.insert(std::make_pair(Nickname.Id, Arch));
    if (Inserted.second)
        return true;
    if (!bQuiet)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Duplicate [Debris] name(%s).  First overwritten with second!",
              kDebrisSourceFile, 0xf8, Nickname.get_string());
    }
    return false;
}

int LoadDebrisArchFile(char const* Path, bool bQuiet)
{
    // FUN_00530d20 (cdecl).
    size_t const SizeBefore = G_DebrisArchDb.size();
    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Debris"))
                ReadDebrisArch(&Reader, bQuiet);
        }
    }
    else
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to open debris file[%s].",
              kDebrisSourceFile, 0x147, Path);
    }
    return static_cast<int>(G_DebrisArchDb.size() - SizeBefore);
}

void ShutdownDebrisArchDb()
{
    // FUN_00530fa0: the table's erase(begin, end).
    G_DebrisArchDb.clear();
}

FDebrisArch* FindDebrisArch(ID_String const* Nickname)
{
    // FUN_005309c0: lower_bound (FUN_0052c7a0) and the key test; the record is node + 0x10.
    std::map<unsigned, FDebrisArch>::iterator const It = G_DebrisArchDb.find(Nickname->Id);
    if (It == G_DebrisArchDb.end())
        return nullptr;
    return &It->second;
}

FDebrisArch* GetDefaultDebrisArch()
{
    // FUN_00530a10.
    return &G_DefaultDebrisArch;
}

// ===========================================================================
// Debris
// ===========================================================================

FClientDebrisGameobj::FClientDebrisGameobj()
    : FClientGameobj(EClientGameobjKind::Debris)
{
    // 0x0053065f..0x00530673: +0xb4 = 0, +0xb8 = 0, +0xbc = 1. The death method and the two
    // lifetimes are left for FUN_005301d0.
    Explosion = nullptr;
    Remaining = 0.0f;
    Field_0xbc = 1;
}

FClientDebrisGameobj::~FClientDebrisGameobj()
{
    // FUN_00530160: the explosion's references dropped (FUN_00536170), then GameObject's
    // destructor FUN_005394b0.
    if (Explosion != nullptr)
        ExplosionArchRelease(Explosion);
}

int FClientDebrisGameobj::get_type(uint& OutType) const
{
    // LAB_00530610.
    OutType = 0x80000000u;
    return 0;
}

void FClientDebrisGameobj::create(Archetype::Root* Arch)
{
    // FUN_005304b0 (RET 4): no narrowing, no null test on the new object.
    GameobjCreateOfClass(this, CObject::ClassObject, Arch);
    Object->open(Arch);
}

bool FClientDebrisGameobj::find(unsigned const& Id)
{
    // FUN_0053f330: XOR AL,AL / RET 4.
    (void)Id;
    return false;
}

void FClientDebrisGameobj::update(float Dt)
{
    // LAB_005304d0 (RET 4).
    // 0x005304d0..0x005304f2: the object's update with CObject::UPDATE_DEFAULT (vt+0x50), then
    // GameObject's update FUN_00539c60.
    Object->update(Dt, CObject::UPDATE_DEFAULT);
    FClientGameobj::update(Dt);

    // 0x005304f2..0x00530503: TEST AH,0x41 / JNZ -- nothing once the countdown is not above 0.
    if (!(Remaining > 0.0f))
        return;
    Remaining = Remaining - Dt;

    // 0x0053050f..0x0053054d: a vanishing piece (method 1) with Field_0xbc clear goes at once
    // when it has lived longer than lifetime min (max - remaining > min, TEST AH,0x41 / JNZ).
    int const Method = DeathMethod;
    if (Method == 1 && Field_0xbc == 0)
    {
        if (LifetimeMax - Remaining > LifetimeMin)
            Remaining = 0.0f;
    }

    // 0x00530557..0x00530588: TEST AH,0x41 / JP -- at 0 or below (ordered): exploding (0) goes
    // through slot 108 with no position, vanishing (1) finishes the Gameobj (slot 87).
    if (Remaining <= 0.0f)
    {
        if (Method == 0)
        {
            Slot1b0(nullptr);
            return;
        }
        if (Method == 1)
            Slot15c(true);
    }
}

unsigned* FClientDebrisGameobj::Slot17c(unsigned* OutLod, void* Camera,
                                        unsigned const* CachedLod)
{
    // FUN_005300f0 (RET 0xc): GameObject's pick FUN_00539820 into a local (retail reuses the
    // third argument's stack slot), its return value ignored.
    unsigned Lod = 0;
    FClientGameobj::Slot17c(&Lod, Camera, CachedLod);
    // 0x00530109..0x0053011b: CMP CX,3 / JC (unsigned low word < 3), CMP word,2 / JL (the
    // signed high word below 2 keeps the flag).
    unsigned short const LowWord = static_cast<unsigned short>(Lod & 0xffffu);
    short const HighWord = static_cast<short>(static_cast<unsigned short>(Lod >> 16));
    if (LowWord < 3u || HighWord >= 2)
        Field_0xbc = 0;
    *OutLod = Lod;
    return OutLod;
}

void FClientDebrisGameobj::Slot1b0(Vector const* Pos)
{
    // LAB_005305a0 (RET 4).
    if (Explosion == nullptr)
    {
        // 0x005305f6..0x005305f9: GameObject's slot 108 FUN_0053a880.
        FClientGameobj::Slot1b0(Pos);
        return;
    }
    // 0x005305b4..0x005305e9: Pos, or get_position() (vt+0) when it is null, copied to the
    // stack; FUN_005361e0(Explosion, this, &Copy) into the action list. Unlike GameObject's
    // slot 108 the effect list is not cleared.
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Vector const Copy = *Where;
    Actions.add(Action_Explode::Create(Explosion, this, &Copy));
}

void FClientDebrisGameobj::Init(FDebrisArch const* Arch)
{
    // FUN_005301d0 (RET 4).
    // 0x005301db: GameObject's slot 92 FUN_00539740, called directly. It reads bWatchListed
    // only after get_type, and Debris's get_type (0x80000000) has none of the bits it tests.
    FClientGameobj::Slot170();

    // 0x005301e7..0x00530200: +0xa8 = the death method (the retail dword store overwrites the
    // bWatchListed byte too), +0xac = lifetime min, +0xb0 = lifetime max.
    DeathMethod = Arch->DeathMethod;
    bWatchListed = static_cast<unsigned char>(DeathMethod & 0xff);
    LifetimeMin = Arch->LifetimeMin;
    LifetimeMax = Arch->LifetimeMax;

    // 0x005301fb..0x00530239: the explosion, with its references taken (FUN_00536100).
    if (!Arch->Explosion.IsEmpty())
    {
        Explosion = Archetype::GetExplosion(Arch->Explosion);
        if (Explosion != nullptr)
            ExplosionArchAddRef(Explosion);
    }
    else
    {
        Explosion = nullptr;
    }

    // 0x00530239..0x00530272: the countdown, lifetime min + rand()/32768 * (max - min).
    float const Min = LifetimeMin;
    float const Max = LifetimeMax;
    Remaining = static_cast<float>(rand()) * kRandScale * (Max - Min) + Min;

    // 0x00530278..0x005303ea: the trail -- a one-shot on this Gameobj, pointing along the
    // velocity (LookMatrix) at the centre of mass (the object's vt+0x68 when it has a body,
    // else get_position()), expressed in the object's frame.
    if (!Arch->Trail.IsEmpty())
    {
        Vector Where;
        if (Object->BodyPtr != nullptr)
            Where = Object->get_center_of_mass();
        else
            Where = get_position();
        Vector const Velocity = get_velocity();
        Matrix const Look = LookMatrix(Velocity);
        FTransform World;
        World.Rotation = reinterpret_cast<FMatrix const&>(Look);
        World.Translation.X = Where.x;
        World.Translation.Y = Where.y;
        World.Translation.Z = Where.z;

        // 0x005302eb..0x0053036d: the inverse of the object's frame -- get_orientation (vt+0x10)
        // and the position that follows it: rotation transposed (FUN_00447b60), translation
        // -(M^T * P) (FUN_00447be0, then FUN_00422b80 on the negated floats; FUN_00447bb0
        // packs the two).
        Transform const& ObjectFrame = reinterpret_cast<Transform const&>(get_orientation());
        FMatrix const& Rotation = reinterpret_cast<FMatrix const&>(ObjectFrame.Rotation);
        FVector ObjectPos;
        ObjectPos.X = ObjectFrame.Translation.x;
        ObjectPos.Y = ObjectFrame.Translation.y;
        ObjectPos.Z = ObjectFrame.Translation.z;
        FVector Rotated;
        ShipTransposeRotate(Rotated, Rotation, ObjectPos); // FUN_00447be0 (0x00530302)
        FTransform Inverse;
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            int Col = 0;
            for (Col = 0; Col < 3; ++Col)
                Inverse.Rotation.D[Row][Col] = Rotation.D[Col][Row];
        }
        Inverse.Translation.X = -Rotated.X;
        Inverse.Translation.Y = -Rotated.Y;
        Inverse.Translation.Z = -Rotated.Z;

        // 0x00530372..0x005303b3: Local = Inverse * World through DAT_00675514's MulTransform
        // (vt+0x1c).
        if (G_DebrisMathEngine == nullptr)
            G_DebrisMathEngine = Acquire3DMathEngine();
        FTransform Local;
        G_DebrisMathEngine->MulTransform(Local, Inverse, World);

        // 0x005303b5..0x005303ea: get_index (vt+0x1c), then -- the trail id tested again --
        // FUN_0040f080 and FUN_00503070 on DAT_00674c60 (EffectArch, this, index, &Local, no
        // companion).
        int const Index = static_cast<int>(get_index());
        if (!Arch->Trail.IsEmpty())
            CliCreateEffectOneShot(FindEffectArch(&Arch->Trail.Id), this, Index, &Local, nullptr);
    }

    // 0x005303f6..0x0053047c: with a physics body, the drag divided by the mass (vt+0x6c) and
    // by the moment of inertia (zero when the body is gone by the second test).
    if (Object->BodyPtr != nullptr)
    {
        float const Linear = Arch->LinearDrag / Object->get_mass();
        Vector Inertia;
        if (Object->BodyPtr != nullptr)
        {
            Inertia = PhySys::GetMomentOfInertia(Object);
        }
        else
        {
            Inertia.x = 0.0f;
            Inertia.y = 0.0f;
            Inertia.z = 0.0f;
        }
        Vector Angular;
        Angular.x = Arch->AngularDrag.x / Inertia.x;
        Angular.y = Arch->AngularDrag.y / Inertia.y;
        Angular.z = Arch->AngularDrag.z / Inertia.z;
        PhySys::SetDamping(Object, Linear, Angular);
    }

    // 0x0053047f..0x00530496: the nebula at get_position() into +0x38, its fog factor into
    // +0x3c (FUN_005553b0, no hint).
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), reinterpret_cast<float*>(&DrawStateB), nullptr);
    DrawStateA = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Nebula));
}

FClientDebrisGameobj* CreateDebrisGameobjForObject(CObject* Object, FDebrisArch const* Arch)
{
    // FUN_00530620 (cdecl).
    FClientDebrisGameobj* const Debris = new FClientDebrisGameobj();
    Debris->Object = Object;                                               // +0x10
    Debris->advise(true);                                                  // CBase::advise
    Debris->Init(Arch);                                                    // FUN_005301d0
    ClientObjDb_RegisterGameobj(Debris, false);                            // FUN_00540c30(, 0)
    return Debris;
}

FClientDebrisGameobj* CreateDebrisGameobjFromArch(Archetype::Root* Arch,
                                                  FDebrisArch const* DebrisArch,
                                                  Vector const* Pos)
{
    // FUN_005306e0 (cdecl).
    FClientDebrisGameobj* const Debris = new FClientDebrisGameobj();
    // 0x0053074c..0x00530779: Arch->load (vt+0xc), its companion's Load(1) (vt+0x20), the
    // Gameobj's create (vt+0x168), Arch->Release.
    Arch->load();
    Arch->Companion->Load(1);
    Debris->create(Arch);
    Arch->Release();

    // 0x0053077c..0x005307af: the object (class & 1, no null test on the result) placed at Pos
    // (+0x2c).
    CObject* const Simple = NarrowDebrisObject(Debris, 1u);
    Simple->WorldPosition = *Pos;

    // 0x005307b2..0x0053082b: a body -- mass 1, the default damping, type 2 with collisions
    // on, no inertia, no name, the current system (FUN_00554e90, read last).
    PhySys::CreateParms Parms;
    std::memset(&Parms, 0, sizeof(Parms));
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.CollisionGroupId = 0;
    Parms.Type = 2;
    Parms.bFlag1c = 0;
    Parms.bEnableCollisions = 1;
    Parms.bHasInertia = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
    Parms.Name[0] = '\0';
    Parms.CollisionGroupId = StarSysGetActiveSystemId();
    PhySys::BuildIVP_Real(Simple, Parms);

    // 0x00530831..0x00530842: FUN_005301d0(DebrisArch), FUN_00540c30(Debris, 0).
    Debris->Init(DebrisArch);
    ClientObjDb_RegisterGameobj(Debris, false);
    return Debris;
}
