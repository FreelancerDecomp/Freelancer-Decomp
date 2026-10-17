// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_archetype.h"
#include "common_archetype_internal.h"

#include "common_cobject.h" // CollisionGroupDesc (get_undamaged_collision_group_list)
#include "common_fuses.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_phyarch.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <cstddef>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace Archetype
{
    namespace
    {

        // listing PTR_s_hp_gun_063eca40 -- name/value pairs. Munition miss -> 4 (hp_gun).
        // Gun hp_gun_type miss -> skip push (not default 4).
        // The table is retail's own, .data:063eca40 .. .data:063ecc37: 0x1f8 bytes, stride 8,
        // 63 entries, decoded dword-for-dword from the listing. It used to stop at 0x26, which
        // left every elite/freighter shield-special class unmapped; the tail below is the rest
        // of the same table and completes HpAttachmentType 4..0x42.
        struct FHpTypeEntry
        {
            char const* Text;
            unsigned Value;
        };

        FHpTypeEntry const* HpTypeTable(unsigned* OutCount)
        {
            static FHpTypeEntry const kTable[] = {
                {"hp_gun", 4},
                {"hp_shield_generator", 5},
                {"hp_fighter_shield_generator", 6},
                {"hp_elite_shield_generator", 7},
                {"hp_freighter_shield_generator", 8},
                {"hp_thruster", 9},
                {"hp_torpedo", 0xa},
                {"hp_mine_dropper", 0xb},
                {"hp_turret", 0xd},
                {"hp_countermeasure_dropper", 0xc},
                {"hp_cargo_pod", 0xe},
                {"hp_gun_special_1", 0xf},
                {"hp_gun_special_2", 0x10},
                {"hp_gun_special_3", 0x11},
                {"hp_gun_special_4", 0x12},
                {"hp_gun_special_5", 0x13},
                {"hp_gun_special_6", 0x14},
                {"hp_gun_special_7", 0x15},
                {"hp_gun_special_8", 0x16},
                {"hp_gun_special_9", 0x17},
                {"hp_gun_special_10", 0x18},
                {"hp_turret_special_1", 0x19},
                {"hp_turret_special_2", 0x1a},
                {"hp_turret_special_3", 0x1b},
                {"hp_turret_special_4", 0x1c},
                {"hp_turret_special_5", 0x1d},
                {"hp_turret_special_6", 0x1e},
                {"hp_turret_special_7", 0x1f},
                {"hp_turret_special_8", 0x20},
                {"hp_turret_special_9", 0x21},
                {"hp_turret_special_10", 0x22},
                {"hp_torpedo_special_1", 0x24},
                {"hp_torpedo_special_2", 0x23},
                {"hp_fighter_shield_special_1", 0x25},
                {"hp_fighter_shield_special_2", 0x26},
                {"hp_fighter_shield_special_3", 0x27},
                {"hp_fighter_shield_special_4", 0x28},
                {"hp_fighter_shield_special_5", 0x29},
                {"hp_fighter_shield_special_6", 0x2a},
                {"hp_fighter_shield_special_7", 0x2b},
                {"hp_fighter_shield_special_8", 0x2c},
                {"hp_fighter_shield_special_9", 0x2d},
                {"hp_fighter_shield_special_10", 0x2e},
                {"hp_elite_shield_special_1", 0x2f},
                {"hp_elite_shield_special_2", 0x30},
                {"hp_elite_shield_special_3", 0x31},
                {"hp_elite_shield_special_4", 0x32},
                {"hp_elite_shield_special_5", 0x33},
                {"hp_elite_shield_special_6", 0x34},
                {"hp_elite_shield_special_7", 0x35},
                {"hp_elite_shield_special_8", 0x36},
                {"hp_elite_shield_special_9", 0x37},
                {"hp_elite_shield_special_10", 0x38},
                {"hp_freighter_shield_special_1", 0x39},
                {"hp_freighter_shield_special_2", 0x3a},
                {"hp_freighter_shield_special_3", 0x3b},
                {"hp_freighter_shield_special_4", 0x3c},
                {"hp_freighter_shield_special_5", 0x3d},
                {"hp_freighter_shield_special_6", 0x3e},
                {"hp_freighter_shield_special_7", 0x3f},
                {"hp_freighter_shield_special_8", 0x40},
                {"hp_freighter_shield_special_9", 0x41},
                {"hp_freighter_shield_special_10", 0x42},
            };
            if (OutCount != nullptr)
                *OutCount = sizeof(kTable) / sizeof(kTable[0]);
            return kTable;
        }
    }

    // Helpers shared with the other Archetype TUs (common_archetype_ships / _equipment /
    // _weapons / _misc.cpp); declared in common_archetype_internal.h.
    namespace CmnArchetype
    {
        Equipment* LookupLootAppearance(char const* Name)
        {
            ID_String Key;
            Key.Id = CreateID(Name);
            return GetEquipmentByName(Key);
        }

        // The linear walk every hp-type key inlines (Ship::read 062f2549.., Gun::read 062f659e..,
        // Munition::read 062f72b2..): DACOM.DLL stricmp against each name of the 63-entry table
        // at .data:063eca40 (stride 8, bound 0x1f8), first match wins.
        bool TryLookupHpType(char const* Name, unsigned* OutValue)
        {
            unsigned Count = 0;
            FHpTypeEntry const* Table = HpTypeTable(&Count);
            unsigned Index = 0;
            for (; Index < Count; ++Index)
            {
                if (CallDacomStricmp(Name, Table[Index].Text) == 0)
                {
                    *OutValue = Table[Index].Value;
                    return true;
                }
            }
            return false;
        }

        bool ReadLootAppearance(INI_Reader& Reader, Equipment*& Out)
        {
            if (!Reader.is_value("loot_appearance"))
                return false;
            Out = LookupLootAppearance(Reader.get_value_string());
            return true;
        }

        // The hp-type arrays of ShieldGenerator (+0xa8) and Gun (+0xbc) are retail VC6
        // vector<unsigned>s {allocator byte, first, last, end}; the port keeps the three
        // pointers as a malloc'd block (the owners' destructors free it).

        // The VC6 vector copy constructor (ShieldGenerator .text:0626e32e..0626e39c, Gun
        // .text:0626eb3a..0626eb9f): a block of exactly size() elements -- operator new(0) for an
        // empty or never-allocated source still answers a block -- the elements copied, and
        // last = end = first + size.
        void CopyConstructHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                                  unsigned const* SrcBegin, unsigned const* SrcEnd)
        {
            unsigned const Count = SrcBegin == nullptr ? 0u : (unsigned)(SrcEnd - SrcBegin);
            Begin = (unsigned*)malloc(Count * sizeof(unsigned));
            unsigned Index = 0;
            for (; Index < Count; ++Index)
                Begin[Index] = SrcBegin[Index];
            End = Begin + Count;
            Cap = End;
        }

        // The VC6 vector operator= (ShieldGenerator .text:0626e484..0626e5c3, Gun
        // .text:0626ece5..0626ee27); the caller skips it for the same vector. n = the source
        // size: n <= size -> the elements overwritten, last = first + n; n <= capacity -> the
        // live ones overwritten and the rest copied in place (FUN_0630cb40 + _Ucopy), last =
        // first + n; else the block destroyed and freed, a block of exactly n, copied, last =
        // end = first + n. The first two cases leave the same state, so they share one path.
        void AssignHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                           unsigned const* SrcBegin, unsigned const* SrcEnd)
        {
            unsigned const Count = SrcBegin == nullptr ? 0u : (unsigned)(SrcEnd - SrcBegin);
            unsigned const Capacity = Begin == nullptr ? 0u : (unsigned)(Cap - Begin);
            if (Count > Capacity)
            {
                free(Begin);
                Begin = (unsigned*)malloc(Count * sizeof(unsigned));
                Cap = Begin + Count;
            }
            unsigned Index = 0;
            for (; Index < Count; ++Index)
                Begin[Index] = SrcBegin[Index];
            End = Begin + Count;
        }

        // Slot 0/1. Root::~Root already inlines the slot-1 Release call directly (see below);
        // these two wrappers exist only for Equipment::InheritFrom's companion AddRef/Inherit/
        // Release dance (Common.dll.c 127869-127873, 128002-128005), matching the paired
        // slot-0/slot-1 calls FUN_062f3ea0 makes around its CliObjInherit call.
        void CliObjAddRef(ICliObj* Companion)
        {
            if (Companion == nullptr)
                return;

            Companion->AddRef();
        }

        void CliObjRelease(ICliObj* Companion)
        {
            if (Companion == nullptr)
                return;

            Companion->Release();
        }
    }

    std::map<unsigned, unsigned short> GLargeToSmall;
    std::vector<unsigned> GSmallToLarge;

    void RegisterLargeId(unsigned LargeId)
    {
        std::map<unsigned, unsigned short>::iterator Existing = GLargeToSmall.find(LargeId);
        if (Existing != GLargeToSmall.end())
            return;

        GSmallToLarge.push_back(LargeId);
        unsigned short SmallId = (unsigned short)GSmallToLarge.size();
        GLargeToSmall.insert(std::pair<unsigned, unsigned short>(LargeId, SmallId));
    }

    // ArchDB::Insert's zero-id arm (FUN_062fbeb0 / FUN_062fbbe0, 062fbbe0..062fbcbe).
    void ArchDBDumpNoNickname(char const* KindName)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Loaded '%s' has no nickname .. not adding to database.\n",
              ArchetypeSource, 0x84, KindName);
    }

    // ArchDB::Get(unsigned)'s miss dump.
    void ArchDBDumpMiss(unsigned NicknameId)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: ArchDB::Get(%u) failed",
              ArchetypeSource, 0x3c, NicknameId);
    }

    // ArchDB::Get(ID_String const&)'s miss dump.
    void ArchDBDumpMiss(ID_String const& Name)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: ArchDB::Get(%s) failed. ID=%d",
              ArchetypeSource, 0x49, Name.get_string(), Name.Id);
    }

    void ClearAllArchDBs()
    {
        GSimpleDb.Clear();
        GShipDb.Clear();
        GSolarDb.Clear();
        GEquipmentDb.Clear();
        GAsteroidDb.Clear();
        GDynamicAsteroidDb.Clear();
        GMotorDb.Clear();
        GExplosionDb.clear();
    }

    ArchDB<Root> GSimpleDb(true);
    ArchDB<Ship> GShipDb(true);
    ArchDB<Solar> GSolarDb(true);
    ArchDB<Equipment> GEquipmentDb(true);  // FUN_062fbbe0 registers small ids too (062fbc26..062fbc88)
    ArchDB<Asteroid> GAsteroidDb(true);
    ArchDB<DynamicAsteroid> GDynamicAsteroidDb(true);
    ArchDB<MotorData> GMotorDb(true);

    unsigned short __cdecl LargeIDToSmallID(unsigned LargeId)
    {
        std::map<unsigned, unsigned short>::const_iterator Found = GLargeToSmall.find(LargeId);
        if (Found == GLargeToSmall.end())
            return 0;
        return Found->second;
    }

    unsigned __cdecl SmallIDToLargeID(unsigned short SmallId)
    {
        if (SmallId == 0)
            return 0;

        unsigned Count = (unsigned)GSmallToLarge.size();
        if (SmallId > Count)
            return 0;

        return GSmallToLarge[SmallId - 1];
    }

    namespace
    {
        struct FObjectTypeEntry
        {
            char const* Name;
            unsigned Bits;
        };

        // PTR_DAT_063ec810, 29 entries, stride 8, loop bound 0xe8 (DecodeType 0x8f3d0).
        FObjectTypeEntry const GObjectTypes[] = {
            { "NONE", 0 },
            { "MOON", 1 },
            { "PLANET", 2 },
            { "SUN", 4 },
            { "ASTEROID", 0x20000000 },
            { "BLACKHOLE", 8 },
            { "SATELLITE", 0x10 },
            { "DOCKING_RING", 0x20 },
            { "JUMP_GATE", 0x40 },
            { "JUMP_HOLE", 0x800 },
            { "WEAPONS_PLATFORM", 0x1000 },
            { "DESTROYABLE_DEPOT", 0x2000 },
            { "NON_TARGETABLE", 0x4000 },
            { "MISSION_SATELLITE", 0x8000 },
            { "AIRLOCK_GATE", 0x400 },
            { "TRADELANE_RING", 0x80 },
            { "STATION", 0x100 },
            { "WAYPOINT", 0x200 },
            { "FIGHTER", 0x10000 },
            { "FREIGHTER", 0x20000 },
            { "GUNBOAT", 0x40000 },
            { "CRUISER", 0x80000 },
            { "TRANSPORT", 0x100000 },
            { "CAPITAL", 0x200000 },
            { "MINING", 0x400000 },
            { "GUIDED", 0x1000000 },
            { "MINE", 0x4000000 },
            { "LOOT", 0x10000000 },
            { "BULLET", 0x2000000 },
        };

        bool CliObjReadUnknown(ICliObj* Companion, INI_Reader& Reader)
        {
            if (Companion == nullptr)
                return false;

            return Companion->ReadUnknown(&Reader);
        }

        void CliObjRedefineCompanion(ICliObj* Companion, ICliObj* Donor)
        {
            if (Companion == nullptr)
                return;

            Companion->Redefine(Donor);
        }
    }

}  // namespace Archetype

// .text:062ef3d0-062ef432, ?DecodeType@@YAIPBD@Z -- a GLOBAL export (declared in
// common_archetype.h). Walks the 29 {Name, Bits} pairs of PTR_DAT_063ec810 (EDI += 8 until
// 0xe8): a null Name is skipped, otherwise stricmp(Name, Entry.Name) == 0 returns Entry.Bits;
// no match -> FDUMP(0x100002, "...unknown OBJECT_TYPE = '%s'", __FILE__, 0xe1, Name) and 0.
// No null test of Name in retail. Keys Collision::Load's damage-by-object-type map
// (common_physys.cpp) and the archetype `type =` handler.
unsigned __cdecl DecodeType(char const* Name)
{
    unsigned Count = sizeof(Archetype::GObjectTypes) / sizeof(Archetype::GObjectTypes[0]);
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (Archetype::GObjectTypes[Index].Name == nullptr)
            continue;
        // 062ef3d6: DACOM.DLL stricmp, not the CRT one.
        if (CallDacomStricmp(Name, Archetype::GObjectTypes[Index].Name) == 0)
            return Archetype::GObjectTypes[Index].Bits;
    }

    FDUMP(0x100002, "%s(%d) : *** WARNING: unknown OBJECT_TYPE = '%s'\n",
          Archetype::ArchetypeSource, 0xe1, Name);
    return 0;
}

namespace Archetype
{

    void CliObjLod(ICliObj* Companion, INI_Reader* Reader)
    {
        if (Companion == nullptr || Reader == nullptr)
            return;

        Companion->Lod(Reader);
    }

    void CliObjBind(ICliObj* Companion, Root* Record)
    {
        if (Companion == nullptr || Record == nullptr)
            return;

        Companion->Bind(Record);
    }

    void CliObjFreeResources(ICliObj* Companion)
    {
        if (Companion == nullptr)
            return;

        Companion->FreeResources();
    }

    void CliObjInherit(ICliObj* Companion, ICliObj* Donor)
    {
        if (Companion == nullptr)
            return;

        Companion->Inherit(Donor);
    }

    unsigned long CliObjLoad(ICliObj* Companion, int LoadMode)
    {
        if (Companion == nullptr)
            return 0;

        return Companion->Load(LoadMode);
    }

    Root::Root(ICliObj* InCompanion)
        : Companion(InCompanion)
        , ObjectType(0)
        , IdsName(0)
        , IdsInfo(0)
        , HitPts(100.0f)
        , Mass(1.0f)
        , ExplosionResistance(1.0f)
        , RotationInertiaX(0.0f)
        , RotationInertiaY(0.0f)
        , RotationInertiaZ(0.0f)
        , bHasRotationInertia(false)
        , bPhantomPhysics(false)
        , LoadCount(0)
        , EngineIndex(-1)
        , AnimIndex(-1)
    {
        Nickname.clear();
        DaArchetype.clear();
        ExplosionArch.clear();
    }

    // Common.dll.txt:258697-258760 (0x901f0). Retail clears Nickname (+0x08), DaArchetype
    // (+0x0c) and ExplosionArch (+0x24), constructs an empty surfaces map at +0x48, stores the
    // Root vptr and calls operator=(Other). Nothing else is initialised first -- in particular
    // Companion (+0x04), which operator= releases before taking Other's, is left as it came
    // (retail reads it uninitialised too; the only callers are the leaf copy constructors,
    // themselves reachable only through the export table). The empty map is the port's
    // representation of it (see Root::Root).
    Root::Root(Root const& Other)
    {
        // Retail 0x062f01f0: the three string members cleared, an empty surfaces map at +0x48,
        // the vptr, then operator=(Other). Retail UB: +0x04 is never initialised, so the
        // operator='s Release of the old companion runs on whatever the memory held; the port
        // starts it at null so that Release is skipped (deterministic).
        Companion = nullptr;
        Nickname.clear();
        DaArchetype.clear();
        ExplosionArch.clear();
        operator=(Other);
    }

    Root::~Root()
    {
        if (Companion != nullptr)
        {
            Companion->Release();
            Companion = nullptr;
        }
    }

    bool Root::read(INI_Reader& Reader)
    {
        if (Reader.is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("DA_archetype") || Reader.is_value("attachment_archetype"))
        {
            DaArchetype.Value = StringAlloc(Reader.get_value_string(0), true);
            return true;
        }

        if (Reader.is_value("type"))
        {
            ObjectType = ::DecodeType(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("ids_name"))
        {
            IdsName = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info"))
        {
            IdsInfo = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("explosion_arch"))
        {
            ExplosionArch.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("explosion_resistance"))
        {
            ExplosionResistance = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("hit_pts"))
        {
            HitPts = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("mass"))
        {
            Mass = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("rotation_inertia"))
        {
            RotationInertiaX = Reader.get_value_float(0);
            RotationInertiaY = Reader.get_value_float(1);
            RotationInertiaZ = Reader.get_value_float(2);
            bHasRotationInertia = true;
            return true;
        }

        if (Reader.is_value("phantom_physics"))
        {
            bPhantomPhysics = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("mission_property"))
            return true;

        return CliObjReadUnknown(Companion, Reader);
    }

    void Root::redefine(Root const& Other)
    {
        CliObjRedefineCompanion(Companion, Other.Companion);

        if (Other.IdsName != IdsName)
            IdsName = Other.IdsName;
        if (Other.IdsInfo != IdsInfo)
            IdsInfo = Other.IdsInfo;
        if (Other.HitPts != HitPts)
            HitPts = Other.HitPts;
        if (Other.ExplosionArch.Id != ExplosionArch.Id)
            ExplosionArch.Id = Other.ExplosionArch.Id;
        if (Other.ExplosionResistance != ExplosionResistance)
            ExplosionResistance = Other.ExplosionResistance;
        if (Other.Mass != Mass)
            Mass = Other.Mass;
        if (Other.RotationInertiaX != RotationInertiaX
            || Other.RotationInertiaY != RotationInertiaY
            || Other.RotationInertiaZ != RotationInertiaZ)
        {
            RotationInertiaX = Other.RotationInertiaX;
            RotationInertiaY = Other.RotationInertiaY;
            RotationInertiaZ = Other.RotationInertiaZ;
        }
    }

    // Common.dll.txt:258901-259123. CORRECTED (D51, 2026-09-09): real retail logic, reachable
    // from Archetype::LoadShips/LoadSolar's "inherit=" donor-copy sequence via
    // EqObj::operator= -- see tracker items 71875/71899 for the full XREF trail that overturned
    // the prior "confirmed dead" verdict. Traced field-by-field against the raw disasm (no
    // top-level self-assignment guard in retail either -- every step below is independently
    // self-assign-safe: CliObjRelease+CliObjAddRef net to an unchanged refcount, PhyArch::
    // the map's own operator= is self-assign-safe, and the remaining fields are POD X=X copies):
    //   - Companion (+0x04): standard smart-pointer assignment -- Release() the old value
    //     (vtable slot 1) if non-null, assign, then AddRef() the new value (vtable slot 0) if
    //     non-null. Reuses the CliObjRelease/CliObjAddRef helpers already written for
    //     Equipment::InheritFrom's own companion dance.
    //   - PhyParts (+0x48): inlined VC6 std::map<uint,Part>::operator= in retail; here the
    //     std::map member's own operator=.
    //   - EngineIndex/AnimIndex (+0x40/+0x44): copied as PLAIN ints, no addref/release/clone of
    //     any kind. This settles the "resource-ownership hazard" the port's previous
    //     `= delete` was hedging against: retail treats these as shared lookup-table indices,
    //     not exclusively-owned handles, at this layer -- so a raw copy is exactly what retail
    //     does, not a simplification.
    //   - every other scalar/ID_String/CacheString field: raw copy.
    Root const& Root::operator=(Root const& Other)
    {
        CliObjRelease(Companion);
        Companion = Other.Companion;
        CliObjAddRef(Companion);

        PhyParts = Other.PhyParts;

        Nickname = Other.Nickname;
        DaArchetype = Other.DaArchetype;
        ObjectType = Other.ObjectType;
        IdsName = Other.IdsName;
        IdsInfo = Other.IdsInfo;
        HitPts = Other.HitPts;
        Mass = Other.Mass;
        ExplosionArch = Other.ExplosionArch;
        ExplosionResistance = Other.ExplosionResistance;
        RotationInertiaX = Other.RotationInertiaX;
        RotationInertiaY = Other.RotationInertiaY;
        RotationInertiaZ = Other.RotationInertiaZ;
        bHasRotationInertia = Other.bHasRotationInertia;
        bPhantomPhysics = Other.bPhantomPhysics;
        LoadCount = Other.LoadCount;
        EngineIndex = Other.EngineIndex;
        AnimIndex = Other.AnimIndex;

        return *this;
    }

    bool __cdecl Load(IFileSystem* Filesys, long* EngineIndex, int* AnimIndex)
    {
        // Retail 0x062efd80. No test of Filesys or of DALib::Engine; DALib::Anim and both out
        // pointers are tested.
        IAnimation2* Anim = DALib::Anim;
        if (Anim != nullptr && AnimIndex != nullptr && *AnimIndex == -1)
        {
            // .text:062efdab..062efdb2: IAnimation2 slot 3 (+0x0c) OpenScript(Filesys).
            *AnimIndex = (int)(ptrdiff_t)Anim->OpenScript(Filesys);
        }

        if (EngineIndex != nullptr)
        {
            // .text:062efdbf..062efe03: the file system's own path (+0x84, 0x100 chars) named
            // with its CRC, IEngine slot 17 (+0x44) FindArchetype.
            char PathBuf[0x100];
            Filesys->GetFileSysPath(PathBuf, 0x100);
            FEngineArchetypeName Name;
            Name.Text = PathBuf;
            Name.Crc = DACOM_CRC::GetCRC32(PathBuf);
            *EngineIndex = (long)DALib::Engine->FindArchetype(&Name);
            if (*EngineIndex == -1)
            {
                // .text:062efe07..062efe31: the CRC taken again, slot 12 (+0x30)
                // CreateArchetypeHandle(&name, Filesys).
                Name.Text = PathBuf;
                Name.Crc = DACOM_CRC::GetCRC32(PathBuf);
                *EngineIndex = (long)DALib::Engine->CreateArchetypeHandle(&Name, Filesys);
            }
        }
        return true;
    }

    bool __cdecl Load(char const* Path, long* EngineIndex, int* AnimIndex)
    {
        // Retail 0x062efe40. No test of Path.
        if (EngineIndex != nullptr && AnimIndex == nullptr)
        {
            // .text:062efe6d..062efe93: an engine-only request first tries the already-loaded
            // archetype by its path (IEngine slot 17, +0x44).
            FEngineArchetypeName Name;
            Name.Text = Path;
            Name.Crc = DACOM_CRC::GetCRC32(Path);
            *EngineIndex = (long)DALib::Engine->FindArchetype(&Name);
            if (*EngineIndex != -1)
                return true;
        }

        // .text:062efea2..062eff02: a 0x34-byte "FileSystem" descriptor with no implementation
        // name (desc+8 zeroed by REP STOSD), the path, GENERIC_READ, FILE_SHARE_READ,
        // OPEN_EXISTING, 0x8000080 and -1 at +0x30, through DALib::Data slot 3 (+0x0c)
        // CreateInstance. A non-zero result is the warning below and false.
        FFileSystemDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Path = Path;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.Field30 = 0xFFFFFFFF;

        IFileSystem* Filesys = nullptr;
        if (DALib::Data->CreateInstance((AGGDESC*)&Desc, (void**)&Filesys) != 0)
        {
            // .text:062effc0..062effed.
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: failed to open archetype (%s) file system\n",
                  ArchetypeSource, 0x1c3, Path);
            return false;
        }

        // .text:062eff0a..062effa3: the body of Load(IFileSystem*, ...) inlined.
        Load(Filesys, EngineIndex, AnimIndex);

        // .text:062effa6..062effaf: Release (+0x08) on the file system, true.
        Filesys->Release();
        return true;
    }

    bool Root::load()
    {
        bool bLoaded = false;
        if (EngineIndex == -1)
        {
            const char* Path = DaArchetype.Value;
            if (Path[0] != '\0')
            {
                bLoaded = Load(Path, &EngineIndex, &AnimIndex);
                if (!bLoaded)
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: missing Archetype file %s\n",
                          ArchetypeSource, 0x2b0, Path);
                    LoadCount += 1;
                    return false;
                }
                if (EngineIndex != -1)
                {
                    char Drive[4];
                    char Dir[260];
                    char Name[256];
                    char Ext[256];
                    char SurPath[260];
                    Drive[0] = '\0';
                    Dir[0] = '\0';
                    Name[0] = '\0';
                    Ext[0] = '\0';
                    SurPath[0] = '\0';
                    _splitpath(Path, Drive, Dir, Name, Ext);
                    // 062f0a86: DACOM.DLL stricmp, not the CRT one.
                    if (CallDacomStricmp(Ext, ".sph") != 0)
                    {
                        _makepath(SurPath, Drive, Dir, Name, "sur");
                        PhyArch::LoadSurfaces(SurPath, PhyParts);
                    }
                }
                bLoaded = true;
            }
        }

        LoadCount += 1;
        return bLoaded;
    }

    // Retail 0x90be0. Unlike load(), does not call the engine mesh loader (Load()); only
    // (re)loads the .sur surfaces map for an already-loaded mesh.
    bool Root::init_physical_representation()
    {
        if (EngineIndex == -1)
            return false;

        char const* Path = DaArchetype.Value;
        char Drive[4] = {0};
        char Dir[260] = {0};
        char Name[256] = {0};
        char Ext[256] = {0};
        char SurPath[260] = {0};
        _splitpath(Path, Drive, Dir, Name, Ext);
        // 062f0c24: DACOM.DLL stricmp, not the CRT one.
        if (CallDacomStricmp(Ext, ".sph") == 0)
            return true;

        _makepath(SurPath, Drive, Dir, Name, "sur");
        return PhyArch::LoadSurfaces(SurPath, PhyParts);
    }

    // Retail 0x90c90. Frees the +0x48 surfaces map only -- does not touch EngineIndex/
    // AnimIndex/Companion (unlike free_resources()). The map's clear() is that walk-and-free.
    void Root::free_physical_representation()
    {
        PhyParts.clear();
    }

    AClassType Root::get_class_type() const
    {
        return AClassRoot;
    }

    bool Root::is_loaded() const
    {
        return EngineIndex != -1;
    }

    PhyArch::Part const* Root::get_phy_part(unsigned PartId) const
    {
        // Retail 0x90d70: map find at this+0x48; a miss returns null, a hit the node's Part.
        std::map<unsigned int, PhyArch::Part>::const_iterator It = PhyParts.find(PartId);
        if (It == PhyParts.end())
            return nullptr;
        return &It->second;
    }

    long Root::create_instance(IEngineInstance* EngineInstance) const
    {
        // 062f05d0. Loaded -> IEngine slot 23 (+0x5C, CreateInstance)(EngineIndex, instance),
        // its result returned as is; no engine or handle test.
        if (EngineIndex != -1)
        {
            return (long)DALib::Engine->CreateInstance((void*)EngineIndex, EngineInstance);
        }

        // The CacheString always holds a pointer (clear() hands out the pool's empty string);
        // retail reads its first byte directly.
        char const* Path = DaArchetype.Value;
        if (Path[0] == '\0')
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Cannot create_instance(). Archetype (%s) has no file assigned. NOT IGNORABLE!\n",
                  ArchetypeSource, 0x218, Nickname.get_string());
            return -1;
        }
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Cannot create_instance(). Archetype (%s) file isn't loaded/valid: %s.  NOT IGNORABLE!\n",
              ArchetypeSource, 0x21e, Nickname.get_string(), Path);
        return -1;
    }

    void Root::AddRef()
    {
        ++LoadCount;
    }

    int Root::GetRef() const
    {
        return LoadCount;
    }

    int Root::Release()
    {
        // 062f0d50: the decremented count is kept in ESI across the slot-5 call and returned.
        int const Count = LoadCount - 1;
        LoadCount = Count;
        if (Count == 0)
            free_resources();
        return Count;
    }

    void Root::free_resources()
    {
        // Retail 0x062f0af0.
        // .text:062f0af6..062f0b0c: a loaded script is closed through DALib::Anim slot 4
        // (+0x10, CloseScript) and the index reset; DALib::Anim itself is not tested.
        if (AnimIndex != -1)
        {
            DALib::Anim->CloseScript((void*)(ptrdiff_t)AnimIndex);
            AnimIndex = -1;
        }

        // .text:062f0b13..062f0b27: a loaded mesh is released through DALib::Engine slot 15
        // (+0x3c, ReleaseHandle).
        if (EngineIndex != -1)
            DALib::Engine->ReleaseHandle((void*)EngineIndex);

        // .text:062f0b2a..062f0bb6: the surfaces map at +0x48 emptied (the inline erase(begin,
        // end) / clear of the VC6 tree).
        PhyParts.clear();

        // .text:062f0bb9: the mesh index reset unconditionally.
        EngineIndex = -1;

        // .text:062f0bc0..062f0bce: the companion's slot 6 (+0x18) when there is one.
        CliObjFreeResources(Companion);
    }

    // Typed Get wrappers — FDUMP on miss via ArchDB::Get.
    Ship* __cdecl GetShip(unsigned NicknameId)
    {
        return GShipDb.Get(NicknameId);
    }

    Ship* __cdecl GetShipByName(ID_String const& Name)
    {
        return GShipDb.Get(Name);
    }

    Solar* __cdecl GetSolar(unsigned NicknameId)
    {
        return GSolarDb.Get(NicknameId);
    }

    Solar* __cdecl GetSolarByName(ID_String const& Name)
    {
        return GSolarDb.Get(Name);
    }

    Equipment* __cdecl GetEquipment(unsigned NicknameId)
    {
        return GEquipmentDb.Get(NicknameId);
    }

    Equipment* __cdecl GetEquipmentByName(ID_String const& Name)
    {
        return GEquipmentDb.Get(Name);
    }

    Root* __cdecl GetSimple(unsigned NicknameId)
    {
        return GSimpleDb.Get(NicknameId);
    }

    Root* __cdecl GetSimpleByName(ID_String const& Name)
    {
        return GSimpleDb.Get(Name);
    }

    Asteroid* __cdecl GetAsteroid(unsigned NicknameId)
    {
        return GAsteroidDb.Get(NicknameId);
    }

    Asteroid* __cdecl GetAsteroidByName(ID_String const& Name)
    {
        return GAsteroidDb.Get(Name);
    }

    DynamicAsteroid* __cdecl GetDynamicAsteroid(unsigned NicknameId)
    {
        return GDynamicAsteroidDb.Get(NicknameId);
    }

    DynamicAsteroid* __cdecl GetDynamicAsteroidByName(ID_String const& Name)
    {
        return GDynamicAsteroidDb.Get(Name);
    }

    MotorData const* __cdecl GetMotor(unsigned NicknameId)
    {
        return GMotorDb.Get(NicknameId);
    }

    Explosion* __cdecl GetExplosion(ID_String const& Name)
    {
        // Retail 0630bd10: map find on DAT_063fcf3c; a miss returns null silently, a hit the
        // address of the Explosion held in the node.
        std::map<unsigned, Explosion>::iterator Found = GExplosionDb.find(Name.Id);
        if (Found == GExplosionDb.end())
            return nullptr;
        return &Found->second;
    }

    // Enum* (062f3310 Ships, 062f3dc0 Solars, 062f9690 Equipment, 062f12e0 Simples,
    // 062fa8a0 Asteroids, 062fa910 DynamicAsteroids): one body each, differing only in the map.
    void __cdecl EnumShips(ArchEnumerator* Enumerator)
    {
        GShipDb.Enumerate(Enumerator);
    }

    void __cdecl EnumSolars(ArchEnumerator* Enumerator)
    {
        GSolarDb.Enumerate(Enumerator);
    }

    void __cdecl EnumEquipment(ArchEnumerator* Enumerator)
    {
        GEquipmentDb.Enumerate(Enumerator);
    }

    void __cdecl EnumSimples(ArchEnumerator* Enumerator)
    {
        GSimpleDb.Enumerate(Enumerator);
    }

    void __cdecl EnumAsteroids(ArchEnumerator* Enumerator)
    {
        GAsteroidDb.Enumerate(Enumerator);
    }

    void __cdecl EnumDynamicAsteroids(ArchEnumerator* Enumerator)
    {
        GDynamicAsteroidDb.Enumerate(Enumerator);
    }

    // Archetype::GetId 062efba0: jump table on ClassType (cases 0,2,3,4,5,6). Cases 0/2/3/4/6
    // inline the FUN_062fb890 walk on their map (Simple 063fcae8, Solar 063fcaac, Ship 063fcac0,
    // Asteroid 063fca98, Equipment 063fcad4); case 5 calls 062fb890 on DynamicAsteroid 063fca84.
    // Case 1 and anything above 6 return 0xFFFFFFFF (062efd5b OR EAX,-1).
    unsigned __cdecl GetId(AClassType ClassType, unsigned Index)
    {
        switch (ClassType)
        {
        case AClassRoot:             return GSimpleDb.KeyAt(Index);
        case AClassSolar:            return GSolarDb.KeyAt(Index);
        case AClassShip:             return GShipDb.KeyAt(Index);
        case AClassAsteroid:         return GAsteroidDb.KeyAt(Index);
        case AClassDynamicAsteroid:  return GDynamicAsteroidDb.KeyAt(Index);
        case AClassEquipment:        return GEquipmentDb.KeyAt(Index);
        default:                     return 0xFFFFFFFFu;
        }
    }

    // Archetype::GetIdCount 062efb40: the map size [map+0x10] for cases 0/2/3/4/5/6 (Simple
    // 063fcaf8, Solar 063fcabc, Ship 063fcad0, Asteroid 063fcaa8, DynamicAsteroid 063fca94,
    // Equipment 063fcae4); case 1 and anything above 6 return 0 (062efb74 XOR EAX,EAX).
    unsigned __cdecl GetIdCount(AClassType ClassType)
    {
        switch (ClassType)
        {
        case AClassRoot:             return GSimpleDb.Size();
        case AClassSolar:            return GSolarDb.Size();
        case AClassShip:             return GShipDb.Size();
        case AClassAsteroid:         return GAsteroidDb.Size();
        case AClassDynamicAsteroid:  return GDynamicAsteroidDb.Size();
        case AClassEquipment:        return GEquipmentDb.Size();
        default:                     return 0;
        }
    }

    int __cdecl GetArchMaxHitPts(unsigned LargeId)
    {
        // Common.dll.c 0x99570 (raw asm authoritative -- ftol confirms this
        // truncates the float HitPts to int, not the float-returning signature
        // the .c pseudocode's misleading "int" cast implied at first glance).
        // Retail checks exactly three DBs in this order -- Ship, Solar,
        // Equipment (NOT Asteroid/Simple/DynamicAsteroid) -- each via
        // ArchDB::Get (which FDUMPs an error on its own miss) and falls
        // through to the next DB rather than returning nullptr, and defaults
        // to 1 (not 0) if all three miss or resolve to a null record.
        Root* Record = GShipDb.Get(LargeId);
        if (Record == nullptr)
            Record = GSolarDb.Get(LargeId);
        if (Record == nullptr)
            Record = GEquipmentDb.Get(LargeId);

        return Record != nullptr ? (int)Record->HitPts : 1;
    }
}

// (ArchetypeLoadFromPath, a client-only port helper that used to be exported from here, now
// lives in freelancer.exe: src/freelancer/Client/client_mesh_load.cpp. Retail Common.dll
// exports no such function.)
