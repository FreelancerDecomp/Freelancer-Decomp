// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype table loaders -- retail Archetype.cpp (LoadSimples 0x062f1000 .. Free
// 0x062fa980 and the per-map teardowns up to 0x062fbe10) plus LoadExplosion 0x0630bd60 /
// FreeExplosions 0x0630c080. Declared in common_archetype.h (namespace Archetype).
#include "common_archetype.h"
#include "common_cliobj.h" // Archetype::ICliObjFactory
#include "common_id.h"     // ID_String, CreateID
#include "common_ini.h"    // INI_Reader
#include "common_misc.h"   // SinglePlayer, SUBOBJ_ID_NONE
#include "dacom.h"         // FDUMP

#include <map>
#include <utility>

namespace
{
    const char kArchetypeSource[] = "E:\\FL\\Scratch\\Source\\Common\\Archetype.cpp";

    // LoadEquipment's equipment sections (062f7eee..062f9248), in the listing's is_header order.
    // FactoryTypeId is the PUSH before ICliObjFactory slot 0 in each branch; it is also the
    // record's get_class_type value, so MakeEquipmentRecord switches on it.
    struct FEquipmentKind
    {
        char const* Name;
        int FactoryTypeId;
    };

    FEquipmentKind const kEquipmentKinds[] = {
        { "LootCrate", 8 },
        { "Commodity", 0xA },
        { "Power", 0xB },
        { "Engine", 0xC },
        { "Armor", 0x22 },
        { "Shield", 0xD },
        { "ShieldGenerator", 0xE },
        { "Thruster", 0xF },
        { "Gun", 0x11 },
        { "MineDropper", 0x12 },
        { "CounterMeasureDropper", 0x13 },
        { "Scanner", 0x14 },
        { "Light", 0x15 },
        { "Tractor", 0x16 },
        { "CargoPod", 9 },
        { "AttachedFX", 0x17 },
        { "InternalFX", 0x18 },
        { "TradeLane", 0x1D },
        { "RepairDroid", 0x19 },
        { "RepairKit", 0x1A },
        { "ShieldBattery", 0x1B },
        { "CloakingDevice", 0x1C },
        { "Munition", 0x1F },
        { "Mine", 0x20 },
        { "CounterMeasure", 0x21 },
    };

    FEquipmentKind const* MatchEquipmentKind(INI_Reader& Reader)
    {
        unsigned Count = sizeof(kEquipmentKinds) / sizeof(kEquipmentKinds[0]);
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_header(kEquipmentKinds[Index].Name))
                return &kEquipmentKinds[Index];
        }
        return nullptr;
    }

    // operator new + the inlined constructor of each LoadEquipment branch (vtable stored after
    // the base ctor, e.g. LootCrate 062f7f1e..062f7f4c with ObjectType 0x10000000, which the
    // port's LootCrate / Commodity ctors set). Keyed by the factory kind, which is unique per
    // section; kEquipmentKinds holds no other value.
    Archetype::Equipment* MakeEquipmentRecord(int FactoryTypeId, Archetype::ICliObj* Companion)
    {
        switch (FactoryTypeId)
        {
        case 0x08: return new Archetype::LootCrate(Companion);
        case 0x0A: return new Archetype::Commodity(Companion);
        case 0x0B: return new Archetype::Power(Companion);
        case 0x0C: return new Archetype::Engine(Companion);
        case 0x22: return new Archetype::Armor(Companion);
        case 0x0D: return new Archetype::Shield(Companion);
        case 0x0E: return new Archetype::ShieldGenerator(Companion);
        case 0x0F: return new Archetype::Thruster(Companion);
        case 0x11: return new Archetype::Gun(Companion);
        case 0x12: return new Archetype::MineDropper(Companion);
        case 0x13: return new Archetype::CounterMeasureDropper(Companion);
        case 0x14: return new Archetype::Scanner(Companion);
        case 0x15: return new Archetype::Light(Companion);
        case 0x16: return new Archetype::Tractor(Companion);
        case 0x09: return new Archetype::CargoPod(Companion);
        case 0x17: return new Archetype::AttachedFXEquip(Companion);
        case 0x18: return new Archetype::InternalFXEquip(Companion);
        case 0x1D: return new Archetype::TradeLaneEquip(Companion);
        case 0x19: return new Archetype::RepairDroid(Companion);
        case 0x1A: return new Archetype::RepairKit(Companion);
        case 0x1B: return new Archetype::ShieldBattery(Companion);
        case 0x1C: return new Archetype::CloakingDevice(Companion);
        case 0x1F: return new Archetype::Munition(Companion);
        case 0x20: return new Archetype::Mine(Companion);
        case 0x21:
        default:   return new Archetype::CounterMeasure(Companion); // the table holds no other id
        }
    }

    // The plain value loop of LoadEquipment (062f7f70..062f7f90) and FUN_062f0dc0
    // (062f0e32..062f0e4c): read() for every key, and on false SinglePlayer() with its result
    // discarded. "inherit" gets no special case here: Equipment::read handles it itself
    // (062f4277), and [Simple] has none. Ships, Solars and Asteroids use ReadShipValues /
    // ReadSolarValues / ReadAsteroidValues below; DynamicAsteroid's loop ignores read()'s result.
    void ReadRootValues(INI_Reader& Reader, Archetype::Root* Record)
    {
        while (Reader.read_value())
        {
            if (!Record->read(Reader))
                SinglePlayer();
        }
    }

    // Insert success tail shared by the loaders: a companion is bound through its slot 5
    // (+0x14) with the record (062f7fb6..062f7fc0, 062f0ef4..062f0efe, 062f2f54..062f2f5e).
    void BindInsertedRecord(Archetype::Root* Record)
    {
        if (Record->Companion != nullptr)
            Record->Companion->Bind(Record);
    }

    // Duplicate tail: the live record is fetched the way ArchDB::Get does (dump at line 0x49 on
    // a miss) and redefined from the new one through slot 2 (+0x08). On a miss -- a section
    // without a nickname (id 0), which the insert rejected -- retail dumps and then calls slot 2
    // through a null pointer (062f92e3 / 062f3035 / 062f0f9d / 062f3ab9 `XOR ECX,ECX;
    // MOV EDX,[ECX]`); the port makes the same call, no null test.
    template <typename T>
    void RedefineLiveRecord(Archetype::ArchDB<T>& Db, T* Record)
    {
        T* Live = Db.Get(Record->Nickname);
        Live->redefine(*Record);
    }

    // [Motor] in LoadEquipment (062f7cc5..062f7eb9): new MotorData (inline ctor), slot 0 read
    // for every value with the result ignored, then the inline insert into DAT_063fca70 with
    // small-id registration (same shape as FUN_062fbeb0; "Motor" in the no-nickname dump).
    // Not inserted (nickname 0 or a duplicate): there is NO reload test -- the live motor is
    // looked up as ArchDB::Get does and redefined (slot 1, 062f7ba0), then the new one freed.
    // On a miss (nickname 0) retail dumps and calls slot 1 through a null pointer (062f7e91
    // XOR ECX,ECX); the port makes the same call, no null test.
    void LoadMotorSection(INI_Reader& Reader)
    {
        Archetype::MotorData* Record = new Archetype::MotorData();
        while (Reader.read_value())
            Record->read(Reader);

        if (Archetype::GMotorDb.Insert(Record, Record->Nickname, "Motor"))
            return;

        Archetype::MotorData* Live = Archetype::GMotorDb.Get(Record->Nickname);
        Live->redefine(*Record);
        delete Record;
    }

    // FUN_062f0dc0 (062f0dc0..062f0ff9), the [Simple] section shared by LoadSimples (062f1089)
    // and LoadShips (062f3166): companion kind 0, Root, value loop, inline insert into
    // DAT_063fcae8 with small-id registration ("Simple" in the no-nickname dump). Not inserted:
    // reload -> redefine the live record and return true; otherwise the [Simple] warning at line
    // 0x364 and return false. The new record is deleted either way.
    bool LoadSimpleSection(INI_Reader& Reader, bool bReload, Archetype::ICliObjFactory* Factory)
    {
        Archetype::ICliObj* Companion = nullptr;
        if (Factory != nullptr)
            Factory->Create(0, &Companion);
        Archetype::Root* Record = new Archetype::Root(Companion);

        ReadRootValues(Reader, Record);

        if (Archetype::GSimpleDb.Insert(Record, Record->Nickname, "Simple"))
        {
            BindInsertedRecord(Record);
            return true;
        }

        bool bResult = false;
        if (bReload)
        {
            RedefineLiveRecord(Archetype::GSimpleDb, Record);
            bResult = true;
        }
        else
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: [Simple] nickname = %s value is not unique. Aborted second version.",
                  kArchetypeSource, 0x364, Record->Nickname.get_string());
        }
        delete Record;
        return bResult;
    }

    // Archetype::Free's per-record teardown, inlined for every Root map (062fa9b0..062faa07 and
    // the same block in FUN_062fabb0 / FUN_062fae00): while the record is still loaded, release
    // it (Solar / Ship / Simple / asteroid maps inline Root::Release; Equipment calls it), counting
    // the turns; a non-zero count is dumped with the nickname; then the scalar deleting dtor.
    void FreeRootRecord(Archetype::Root* Record, char const* Format, int Line)
    {
        if (Record == nullptr)
            return;

        int Dangling = 0;
        if (Record->EngineIndex != -1)
        {
            do
            {
                Record->Release();
                ++Dangling;
            } while (Record->EngineIndex != -1);

            if (Dangling != 0)
                FDUMP(0x100002, Format, kArchetypeSource, Line, Record->Nickname.get_string(), Dangling);
        }
        delete Record;
    }

    void FreeSolarRecord(Archetype::Solar* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Solar %s has %d dangling references.\n",
                       0x7ce);
    }

    void FreeShipRecord(Archetype::Ship* Entry)
    {
        // Retail format really has %x for the nickname string (and no newline); reproduced.
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Ship %x has %d dangling references.",
                       0x6d8);
    }

    void FreeEquipmentRecord(Archetype::Equipment* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Equipment %s has %d dangling references.\n",
                       0x112a);
    }

    void FreeSimpleRecord(Archetype::Root* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::SimpleDB %s has %d dangling references.\n",
                       0x3c4);
    }

    void FreeAsteroidRecord(Archetype::Asteroid* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Asteroid %s has %d dangling references.",
                       0x12a8);
    }

    void FreeDynamicAsteroidRecord(Archetype::DynamicAsteroid* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::DynamicAsteroid %s has %d dangling references.",
                       0x12bc);
    }

    // FUN_062facf0: every non-null MotorData is released with plain operator delete (MotorData
    // has a trivial destructor), then the map is emptied.
    void FreeMotorRecord(Archetype::MotorData* Entry)
    {
        delete Entry;
    }

    // The "inherit =" donor copy is inline in the [Ship], [Solar] and [Asteroid] value loops of
    // the loaders. Each loop tests "inherit" BEFORE handing the line to the record's read(), so
    // the record's read() (and its companion's ReadUnknown) never sees that key. Ship, Solar and
    // Asteroid read() have no "inherit" case: s_inherit_063a17dc is pushed only at 062f2ae0
    // (LoadShips), 062f3705 (LoadSolar), 062f4277 (Equipment::read) and 062fa2c0 (LoadAsteroids).
    //
    // Shape of the copy, the same in all three loops. Companion is the local the factory filled,
    // which is also the record's +0x04.
    //   - Companion, when non-null: slot 0 (AddRef), then slot 7 (Inherit) with the donor's
    //     companion. The Solar loop skips slot 7 when the donor's companion is null.
    //   - Save the record's +0x08 nickname.
    //   - Build a stack temp from the donor (Ship / Solar: EqObj copy ctor; Asteroid: inline
    //     Root ctor + Root::operator=), copy the leaf fields into it, then run the base
    //     operator= from the temp into the record. The temp's destruction releases the companion
    //     it took from the donor.
    //   - The record's +0x04 (now the donor's companion): slot 1 (Release), then the record gets
    //     back Companion and the saved nickname.
    // The port runs the base operator= straight from the donor. Retail UB this drops: the temp's
    // +0x04 is never initialised before Root::operator= releases it (Root(Root const&) at
    // 062f01f0 for Ship / Solar, the inline temp at 062fa396..062fa3d8 for Asteroid). The donor's
    // companion nets to +0 references either way.

    // 062f2ae0..062f2f37: LoadShips' value loop. The donor lookup is ArchDB::Get(unsigned)
    // inlined (dump 0x3c "ArchDB::Get(%u) failed"); no donor -> the 'inherit' warning at 0x649
    // naming ID_String{id}. Any other key goes to Ship::read, and on false to SinglePlayer() with
    // the result discarded.
    void ReadShipValues(INI_Reader& Reader, Archetype::Ship* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                if (!Record->read(Reader))
                    SinglePlayer();
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Ship* Donor = Archetype::GShipDb.Get(DonorId.Id);
            if (Donor == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: 'inherit' failed. couldn't find %s",
                      kArchetypeSource, 0x649, DonorId.get_string());
                continue;
            }

            ID_String const SavedNickname = Record->Nickname;     // 062f2b9a
            if (Companion != nullptr)                              // 062f2ba1..062f2bb7
            {
                Companion->AddRef();
                Companion->Inherit(Donor->Companion);
            }

            Record->EqObj::operator=(*Donor);                      // 062f2d43
            Record->MsgIdPrefix = Donor->MsgIdPrefix.Data;         // 062f2d56 FUN_062fb910
            Record->BayDoorAnim = Donor->BayDoorAnim;              // 062f2d5b..062f2e6c: +0xdc..+0x12c
            Record->HpBaySurface = Donor->HpBaySurface;
            Record->HpBayExternal = Donor->HpBayExternal;
            Record->HpTractorSource = Donor->HpTractorSource;
            Record->IdsInfo1 = Donor->IdsInfo1;
            Record->IdsInfo2 = Donor->IdsInfo2;
            Record->IdsInfo3 = Donor->IdsInfo3;
            Record->ShipClass = Donor->ShipClass;
            Record->NumExhaustNozzles = Donor->NumExhaustNozzles;
            Record->HoldSize = Donor->HoldSize;
            Record->LinearDrag = Donor->LinearDrag;
            Record->AngularDragX = Donor->AngularDragX;
            Record->AngularDragY = Donor->AngularDragY;
            Record->AngularDragZ = Donor->AngularDragZ;
            Record->SteeringTorqueX = Donor->SteeringTorqueX;
            Record->SteeringTorqueY = Donor->SteeringTorqueY;
            Record->SteeringTorqueZ = Donor->SteeringTorqueZ;
            Record->NudgeForce = Donor->NudgeForce;
            Record->StrafeForce = Donor->StrafeForce;
            Record->StrafePowerUsage = Donor->StrafePowerUsage;
            Record->MaxBankAngle = Donor->MaxBankAngle;
            // +0x130: copy-constructed into the temp (062f2d02 FUN_062fb1e0), assigned into the
            // record (062f2e72 FUN_062fb2a0), the temp's copy destroyed (062f2eb0 / 062f2ebd).
            Record->LegalHps = Donor->LegalHps;
            Record->NanobotLimit = Donor->NanobotLimit;            // 062f2e77..062f2e8b +0x140/+0x144
            Record->ShieldBatteryLimit = Donor->ShieldBatteryLimit;

            if (Record->Companion != nullptr)                      // 062f2eed..062f2ef6
                Record->Companion->Release();
            Record->Companion = Companion;                         // 062f2ef9..062f2f04
            Record->Nickname = SavedNickname;
        }
    }

    // 062f3705..062f3959: LoadSolar's value loop. The lookup is ArchDB::Get(ID_String) inlined
    // (dump 0x49 "ArchDB::Get(%s) failed. ID=%d", 062f375b..062f3792); no warning follows it, and
    // a hit whose value is null is skipped silently (062f379f).
    void ReadSolarValues(INI_Reader& Reader, Archetype::Solar* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                if (!Record->read(Reader))                         // 062f392e..062f3941
                    SinglePlayer();
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Solar* Donor = Archetype::GSolarDb.Get(DonorId);
            if (Donor == nullptr)
                continue;

            ID_String const SavedNickname = Record->Nickname;     // 062f37ab
            if (Companion != nullptr)                              // 062f37a5..062f37c2
            {
                Companion->AddRef();
                if (Donor->Companion != nullptr)
                    Companion->Inherit(Donor->Companion);
            }

            Record->EqObj::operator=(*Donor);                      // 062f383d
            Record->bDestructible = Donor->bDestructible;          // 062f37cf..062f3897: +0x94..+0xa8
            Record->SolarRadius = Donor->SolarRadius;
            Record->Toughness = Donor->Toughness;
            Record->OpenAnim = Donor->OpenAnim;
            Record->JumpOutHp = Donor->JumpOutHp;
            Record->Loadout = Donor->Loadout;

            if (Record->Companion != nullptr)                      // 062f3914..062f391d
                Record->Companion->Release();
            Record->Nickname = SavedNickname;                      // 062f3920..062f3927
            Record->Companion = Companion;
        }
    }

    // 062fa2c0..062fa49b: LoadAsteroids' [Asteroid] / [AsteroidMine] value loop. Lookup and miss
    // as LoadShips (dump 0x3c, then the 'inherit' warning at 0x125b). Any other key goes to
    // Asteroid::read with the result ignored (062fa485, no SinglePlayer call).
    void ReadAsteroidValues(INI_Reader& Reader, Archetype::Asteroid* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                Record->read(Reader);
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Asteroid* Donor = Archetype::GAsteroidDb.Get(DonorId.Id);
            if (Donor == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: 'inherit' failed. couldn't find %s",
                      kArchetypeSource, 0x125b, DonorId.get_string());
                continue;
            }

            ID_String const SavedNickname = Record->Nickname;     // 062fa380
            if (Companion != nullptr)                              // 062fa37a..062fa393
            {
                Companion->AddRef();
                Companion->Inherit(Donor->Companion);
            }

            Record->Root::operator=(*Donor);                       // 062fa3d8 / 062fa412
            Record->DetectRadius = Donor->DetectRadius;            // 062fa3dd..062fa432 +0x5c..+0x64
            Record->ExplosionOffset = Donor->ExplosionOffset;
            Record->RechargeTime = Donor->RechargeTime;

            if (Record->Companion != nullptr)                      // 062fa45f..062fa468
                Record->Companion->Release();
            Record->Nickname = SavedNickname;                      // 062fa46b..062fa472
            Record->Companion = Companion;
        }
    }

    // [LOD] in every loader (e.g. 062f924d..062f9286): the last inserted record's companion gets
    // slot 3 (+0x0c) with the reader, when both exist.
    void ApplyLodHeader(INI_Reader& Reader, Archetype::Root* Current)
    {
        if (Current == nullptr || Current->Companion == nullptr)
            return;
        Current->Companion->Lod(&Reader);
    }
}

int __cdecl Archetype::LoadEquipment(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // Retail 062f7c50..062f93e1. The returned count is (motors + equipment) now minus at entry
    // (062f7c6f..062f7c7e, 062f934c..062f935e), and the BEAM_ARCH handle is created at the end
    // even when the file did not open (both open / first read_header failures jump to 062f930e).
    unsigned Saved = GEquipmentDb.Size() + GMotorDb.Size();

    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        Root* Current = nullptr;
        while (Reader.read_header())
        {
            if (Reader.is_header("Motor"))
            {
                Current = nullptr;
                LoadMotorSection(Reader);
            }
            else if (Reader.is_header("Explosion"))
            {
                Current = nullptr;
                LoadExplosion(Reader, bReload);
            }
            else
            {
                FEquipmentKind const* Kind = MatchEquipmentKind(Reader);
                if (Kind != nullptr)
                {
                    ICliObj* Companion = nullptr;
                    if (Factory != nullptr)
                        Factory->Create(Kind->FactoryTypeId, &Companion);

                    Equipment* Record = MakeEquipmentRecord(Kind->FactoryTypeId, Companion);
                    ReadRootValues(Reader, Record);

                    // LAB_062f7f92: FUN_062fbbe0 with the literal "Equipment" for every section.
                    if (GEquipmentDb.Insert(Record, Record->Nickname, "Equipment"))
                    {
                        BindInsertedRecord(Record);
                        Current = Record;
                    }
                    else
                    {
                        // LAB_062f928b: only a reload redefines; no warning on a duplicate.
                        if (bReload)
                            RedefineLiveRecord(GEquipmentDb, Record);
                        delete Record;
                        Current = nullptr;
                    }
                }
                else if (Reader.is_header("LOD"))
                {
                    ApplyLodHeader(Reader, Current);
                }
            }
        }
    }

    Munition::LoadBeamArchIdx();
    return (int)(GEquipmentDb.Size() + GMotorDb.Size() - Saved);
}

int __cdecl Archetype::LoadSolar(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // Retail 062f35b0..062f3ca3. Returns the Solar map size now minus at entry (062f399b; 0 when
    // the file does not open). Header order: Solar, CollisionGroup, Explosion, LOD.
    unsigned Saved = GSolarDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    SubObjectID::CollGroupIdMaker GroupIds;                    // 062f3614
    Solar* Current = nullptr;
    while (Reader.read_header())
    {
        if (Reader.is_header("Solar"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(2, &Companion);                // 062f364d..062f3660

            Solar* Record = new Solar(Companion);              // 062f3662..062f36cd, inline ctor
            GroupIds.Reset();                                  // 062f36e7
            ReadSolarValues(Reader, Record, Companion);
            if (GSolarDb.Insert(Record, Record->Nickname, "Solar"))
            {
                BindInsertedRecord(Record);
                Current = Record;
            }
            else
            {
                // 062f3a6a: no reload test and no warning -- a duplicate always redefines.
                RedefineLiveRecord(GSolarDb, Record);
                delete Record;
                Current = nullptr;
            }
        }
        else if (Reader.is_header("CollisionGroup"))
        {
            // 062f3af0..062f3b39: no current solar -> the ERROR at line 0x780; otherwise the next
            // group id goes straight to FUN_062f9b10 (no SUBOBJ_ID_NONE test here, unlike Ships).
            if (Current == nullptr)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kArchetypeSource, 0x780,
                      "Attempt to load a solar group into a solar that wasn't loaded.\n");
            }
            else
            {
                unsigned short const GroupId = GroupIds.CreateGroupID();
                ReadCollisionGroup(Reader, Current, GroupId);
            }
        }
        else if (Reader.is_header("Explosion"))
        {
            LoadExplosion(Reader, bReload);
        }
        else if (Reader.is_header("LOD"))
        {
            ApplyLodHeader(Reader, Current);
        }
    }

    return (int)(GSolarDb.Size() - Saved);
}

int __cdecl Archetype::LoadShips(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // Retail 062f29d0..062f321e. Returns the Ship map size now minus at entry (062f2f7a; 0 when
    // the file does not open). Header order: Ship, CollisionGroup, LOD, Simple, Explosion.
    unsigned Saved = GShipDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    SubObjectID::CollGroupIdMaker GroupIds;                    // 062f2a33
    Ship* Current = nullptr;
    while (Reader.read_header())
    {
        if (Reader.is_header("Ship"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(3, &Companion);                // 062f2a69..062f2a81

            Ship* Record = new Ship(Companion);                // 062f2a83..062f2aa7
            GroupIds.Reset();                                  // 062f2ac2
            ReadShipValues(Reader, Record, Companion);
            if (GShipDb.Insert(Record, Record->Nickname, "Ship"))
            {
                BindInsertedRecord(Record);
                Current = Record;
            }
            else
            {
                // 062f2fdb: reload -> redefine; otherwise the [Ship] warning at line 0x66b.
                if (bReload)
                    RedefineLiveRecord(GShipDb, Record);
                else
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: [Ship] nickname = %s value is not unique. Aborted second version.",
                          kArchetypeSource, 0x66b, Record->Nickname.get_string());
                delete Record;
                Current = nullptr;
            }
        }
        else if (Reader.is_header("CollisionGroup"))
        {
            // 062f30a0..062f30fa: skipped without a current ship; an id equal to SUBOBJ_ID_NONE
            // (the 16-bit counter wrapped) gets the warning at line 0x67d instead of a group.
            if (Current != nullptr)
            {
                unsigned short const GroupId = GroupIds.CreateGroupID();
                if (GroupId == SUBOBJ_ID_NONE)
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: [Ship] nickname = %s: Exceeds maximum collision group",
                          kArchetypeSource, 0x67d, Current->Nickname.get_string());
                }
                else
                    ReadCollisionGroup(Reader, Current, GroupId);
            }
        }
        else if (Reader.is_header("LOD"))
        {
            ApplyLodHeader(Reader, Current);
        }
        else if (Reader.is_header("Simple"))
        {
            LoadSimpleSection(Reader, bReload, Factory);
        }
        else if (Reader.is_header("Explosion"))
        {
            LoadExplosion(Reader, bReload);
        }
    }

    return (int)(GShipDb.Size() - Saved);
}

int __cdecl Archetype::LoadAsteroids(char const* Path, ICliObjFactory* Factory)
{
    unsigned SavedAsteroids = GAsteroidDb.Size();
    unsigned SavedDynamic = GDynamicAsteroidDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    // Retail 062fa0d0..062fa6bd. Any other header is skipped. Both inserts share the tail
    // 062fa4b0..062fa4cd: inserted -> companion bind; not inserted -> delete, nothing else.
    while (Reader.read_header())
    {
        if (Reader.is_header("Asteroid") || Reader.is_header("AsteroidMine"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(4, &Companion);                // 062fa23f..062fa257

            Asteroid* Record = new Asteroid(Companion);        // 062fa259..062fa29f, inline ctor
            ReadAsteroidValues(Reader, Record, Companion);
            // 062fa49c: "Asteroid" for both headers.
            if (GAsteroidDb.Insert(Record, Record->Nickname, "Asteroid"))
                BindInsertedRecord(Record);
            else
                delete Record;
        }
        else if (Reader.is_header("DynamicAsteroid"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(5, &Companion);                // 062fa19c..062fa1b4

            DynamicAsteroid* Record = new DynamicAsteroid(Companion); // 062fa1b6..062fa1e4
            while (Reader.read_value())                        // 062fa1ef..062fa224: result ignored
                Record->read(Reader);
            if (GDynamicAsteroidDb.Insert(Record, Record->Nickname, "DynamicAsteroid"))
                BindInsertedRecord(Record);
            else
                delete Record;
        }
    }

    return (int)((GAsteroidDb.Size() - SavedAsteroids) +
                 (GDynamicAsteroidDb.Size() - SavedDynamic));
}

int __cdecl Archetype::LoadSimples(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // Retail 062f1000..062f11dc: the Simple map size at entry, [Simple] -> FUN_062f0dc0 (result
    // ignored), [Explosion] -> LoadExplosion, anything else skipped; returns size - saved (0 when
    // the file does not open).
    unsigned Saved = GSimpleDb.Size();

    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Simple"))
                LoadSimpleSection(Reader, bReload, Factory);
            else if (Reader.is_header("Explosion"))
                LoadExplosion(Reader, bReload);
        }
    }

    return (int)(GSimpleDb.Size() - Saved);
}

bool __cdecl Archetype::LoadExplosion(INI_Reader& Reader, bool bReload)
{
    // Retail 0630bd60..0630bfd0. Only the first "nickname" while no record is held counts:
    // Id = CreateID(value) with no zero test, Explosion temp(Id) copied into a pair and inserted
    // by value into DAT_063fcf3c; the record is the node's value whether or not the key was new.
    // An existing key under reload gets the inline reset() (0630bf0a..0630bf82) and the id back.
    // Every other key, once a record is held, goes to Explosion::read (result ignored).
    Explosion* Live = nullptr;
    while (Reader.read_value())
    {
        if (Reader.is_value("nickname"))
        {
            if (Live != nullptr)
                continue;

            ID_String RecordNickname;
            RecordNickname.Id = CreateID(Reader.get_value_string());
            std::pair<std::map<unsigned, Explosion>::iterator, bool> Inserted =
                GExplosionDb.insert(std::pair<unsigned const, Explosion>(
                    RecordNickname.Id, Explosion(RecordNickname)));
            Live = &Inserted.first->second;

            if (!Inserted.second && bReload)
            {
                Live->reset();
                Live->Nickname = RecordNickname;
            }
        }
        else if (Live != nullptr)
        {
            Live->read(Reader);
        }
    }

    return Live != nullptr;
}

void __cdecl Archetype::Free()
{
    // Retail 062fa980..062fabaa, then FUN_062facf0, FUN_062fabb0 and the tail jump to
    // FUN_062fae00. The explosion map is left to FreeExplosions.
    GSolarDb.ForEach(FreeSolarRecord);
    GSolarDb.Clear();
    GShipDb.ForEach(FreeShipRecord);
    GShipDb.Clear();
    GEquipmentDb.ForEach(FreeEquipmentRecord);
    GEquipmentDb.Clear();
    GMotorDb.ForEach(FreeMotorRecord);
    GMotorDb.Clear();
    Munition::FreeBeamArchIdx();
    GSimpleDb.ForEach(FreeSimpleRecord);
    GSimpleDb.Clear();
    GAsteroidDb.ForEach(FreeAsteroidRecord);
    GDynamicAsteroidDb.ForEach(FreeDynamicAsteroidRecord);
    GAsteroidDb.Clear();
    GDynamicAsteroidDb.Clear();
}

void __cdecl Archetype::FreeExplosions()
{
    // Retail 0630c080: erase every node of DAT_063fcf3c (each Explosion destroyed in place).
    GExplosionDb.clear();
}
