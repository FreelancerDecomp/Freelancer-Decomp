// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll ArchDB + Archetype::Root / EqObj.
// ArchDB: FUN_062fbeb0 insert, FUN_06301640 find. Root: ctor 0x8fff0, vftable 063a11f0.
// Linked into freelancer.exe. Do not expose MSVCP60 strings.
// See docs/re/common_archetype.md.

#include "common_id.h"
#include "common_net.h"     // CommReferrable (second base of Archetype::Ship)
#include "common_phyarch.h"

#include <cstddef>

// ?create_instance@Root@Archetype@@QBEJPAUIEngineInstance@@@Z. Defined in common_cobject.h;
// declared here so archetypes do not have to include the object layer.
struct IEngineInstance;
#include <list>
#include <map>
#include <string>
#include <vector>

struct IFileSystem;

// MOVED HERE 2026-09-17 (Server.dll lane, wave 11) from common_cobject.h. It used to be a
// forward declaration, with the definition sitting in the object layer, which meant that
// anything wanting to HOLD a std::list<CollisionGroupDesc> by value had to include the whole
// of common_cobject.h. PlayerData needs exactly that (retail slot+0x26c -- see
// common_server_playerdb.h), and common_server_playerdb.h reaches fourteen translation units.
//
// Moving the struct costs nothing: every translation unit that includes common_cobject.h
// gets this header through common_cobject.h's own `#include "common_archetype.h"`, and this
// header already declares Archetype::EqObj::get_undamaged_collision_group_list(
// std::list<CollisionGroupDesc>&) below, i.e. it was already the type's real user.
//
// Listing CollisionGroupDesc sizeof 8. GetCollisionGroupDesc writes ushort id + float health.
struct COMMON_API CollisionGroupDesc
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    CollisionGroupDesc& operator=(CollisionGroupDesc const&) = default;

    unsigned short Id;            // +0x00
    unsigned short Pad02;
    float Health;                 // +0x04 relative

    bool operator<(CollisionGroupDesc const& Other) const { return Id < Other.Id; }
    bool operator>(CollisionGroupDesc const& Other) const { return Other.Id < Id; }
    bool operator==(CollisionGroupDesc const& Other) const { return Id == Other.Id; }
    bool operator!=(CollisionGroupDesc const& Other) const { return Id != Other.Id; }
};

static_assert(sizeof(CollisionGroupDesc) == 8, "CollisionGroupDesc sizeof 8");

// MOVED HERE 2026-09-24 (claude:srv-pdb) from common_cobject.h -- see the note there.
// SubObjectID: ID generation and classification for equipment/shields/collision groups.
namespace SubObjectID
{
    class COMMON_API CollGroupIdMaker
    {
    public:
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the copy assignment.
        CollGroupIdMaker& operator=(CollGroupIdMaker const&) = default;

        CollGroupIdMaker();
        unsigned short CreateGroupID();
        void Reset();

    private:
        unsigned short NextId;  // +0x00
    };

    class COMMON_API EquipIdMaker
    {
    public:
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the copy assignment.
        EquipIdMaker& operator=(EquipIdMaker const&) = default;

        EquipIdMaker();
        unsigned short CreateEquipID();
        void Reset();

    private:
        unsigned short NextId;  // +0x00
    };

    class COMMON_API ShieldIdMaker
    {
    public:
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the copy assignment.
        ShieldIdMaker& operator=(ShieldIdMaker const&) = default;

        ShieldIdMaker();
        unsigned short CreateShieldID();
        void Reset();

    private:
        unsigned short NextId;  // +0x00
    };

    COMMON_API bool IsArchGroupID(unsigned short Id);
    COMMON_API bool IsEquipID(unsigned short Id);
    COMMON_API bool IsShieldEquipID(unsigned short Id);
}

static_assert(sizeof(SubObjectID::CollGroupIdMaker) == 2, "CollGroupIdMaker sizeof 2");
static_assert(sizeof(SubObjectID::EquipIdMaker) == 2, "EquipIdMaker sizeof 2");
static_assert(sizeof(SubObjectID::ShieldIdMaker) == 2, "ShieldIdMaker sizeof 2");

// Retail Common.dll exports every member of this STL instantiation (VC6 std::list, 55
// members, imported by name by FreeLancer.exe, Server.dll and rpclocal.dll). The port
// exports the modern MSVC instantiation instead: the definition is in common_archetype_ships.cpp; other modules
// import it through this declaration. It must precede the first use that would implicitly
// instantiate the class, which is why it sits directly after the element type.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::list<CollisionGroupDesc>;
#endif

class INI_Reader;

// 4-byte holder of a StringAlloc'd (StringCache-interned) string. Not MSVCP60 std::string.
// Retail exports only clear() (no class-level export: no operator=). clear (06333e30) stores
// the StringCache pool's shared empty string (DAT_0640a810) -- the pointer StringAlloc(null/"")
// also returns -- so it is defined next to the pool in common_stringcache.cpp.
struct CacheString
{
    char const* Value;

    COMMON_API void clear();
};

static_assert(sizeof(CacheString) == 4, "CacheString is a single pointer");

// Retail's global enum HpAttachmentType
// (?get_hp_type_by_index@Gun@Archetype@@QBE?AW4HpAttachmentType@@H@Z). Enumerators leave no
// symbols in a binary, but the whole mapping IS recoverable, and it is recovered here: the
// ship loader's hp_type directive (Common.dll .text:062f2549) walks a 0x1f8-byte table of
// {char const* Name; HpAttachmentType Value;} pairs at Common.dll .data:063eca40 -- 63 entries,
// stricmp against the INI token -- and that table pins every value from 4 to 0x42 to a name.
// The first two entries (.data:063eca40 = "hp_gun"/4, .data:063eca48 = "hp_shield_generator"/5)
// are visible as labels in the listing; the rest decode from the same 8-byte stride.
// 0..3 appear in no table entry -- they are the hard-coded returns of the get_hp_type()
// overrides, and their names say only which override returns them: Equipment's base and the
// out-of-range arm of every get_hp_type_by_index 0, Commodity 1, the internally mounted
// family (Power, Engine, Armor, Scanner, Tractor, RepairDroid, RepairKit, ShieldBattery) 2,
// Munition / Mine 3.
enum HpAttachmentType
{
    HpTypeNone = 0x00,
    HpTypeCommodity = 0x01,
    HpTypeInternal = 0x02,
    HpTypeMunition = 0x03,
    HpTypeGun = 0x04,
    HpTypeShieldGenerator = 0x05,
    HpTypeFighterShieldGenerator = 0x06,
    HpTypeEliteShieldGenerator = 0x07,
    HpTypeFreighterShieldGenerator = 0x08,
    HpTypeThruster = 0x09,
    HpTypeTorpedo = 0x0a,
    HpTypeMineDropper = 0x0b,
    HpTypeCountermeasureDropper = 0x0c,
    HpTypeTurret = 0x0d,
    HpTypeCargoPod = 0x0e,
    HpTypeGunSpecial1 = 0x0f,
    HpTypeGunSpecial2 = 0x10,
    HpTypeGunSpecial3 = 0x11,
    HpTypeGunSpecial4 = 0x12,
    HpTypeGunSpecial5 = 0x13,
    HpTypeGunSpecial6 = 0x14,
    HpTypeGunSpecial7 = 0x15,
    HpTypeGunSpecial8 = 0x16,
    HpTypeGunSpecial9 = 0x17,
    HpTypeGunSpecial10 = 0x18,
    HpTypeTurretSpecial1 = 0x19,
    HpTypeTurretSpecial2 = 0x1a,
    HpTypeTurretSpecial3 = 0x1b,
    HpTypeTurretSpecial4 = 0x1c,
    HpTypeTurretSpecial5 = 0x1d,
    HpTypeTurretSpecial6 = 0x1e,
    HpTypeTurretSpecial7 = 0x1f,
    HpTypeTurretSpecial8 = 0x20,
    HpTypeTurretSpecial9 = 0x21,
    HpTypeTurretSpecial10 = 0x22,
    HpTypeTorpedoSpecial2 = 0x23,
    HpTypeTorpedoSpecial1 = 0x24,
    HpTypeFighterShieldSpecial1 = 0x25,
    HpTypeFighterShieldSpecial2 = 0x26,
    HpTypeFighterShieldSpecial3 = 0x27,
    HpTypeFighterShieldSpecial4 = 0x28,
    HpTypeFighterShieldSpecial5 = 0x29,
    HpTypeFighterShieldSpecial6 = 0x2a,
    HpTypeFighterShieldSpecial7 = 0x2b,
    HpTypeFighterShieldSpecial8 = 0x2c,
    HpTypeFighterShieldSpecial9 = 0x2d,
    HpTypeFighterShieldSpecial10 = 0x2e,
    HpTypeEliteShieldSpecial1 = 0x2f,
    HpTypeEliteShieldSpecial2 = 0x30,
    HpTypeEliteShieldSpecial3 = 0x31,
    HpTypeEliteShieldSpecial4 = 0x32,
    HpTypeEliteShieldSpecial5 = 0x33,
    HpTypeEliteShieldSpecial6 = 0x34,
    HpTypeEliteShieldSpecial7 = 0x35,
    HpTypeEliteShieldSpecial8 = 0x36,
    HpTypeEliteShieldSpecial9 = 0x37,
    HpTypeEliteShieldSpecial10 = 0x38,
    HpTypeFreighterShieldSpecial1 = 0x39,
    HpTypeFreighterShieldSpecial2 = 0x3a,
    HpTypeFreighterShieldSpecial3 = 0x3b,
    HpTypeFreighterShieldSpecial4 = 0x3c,
    HpTypeFreighterShieldSpecial5 = 0x3d,
    HpTypeFreighterShieldSpecial6 = 0x3e,
    HpTypeFreighterShieldSpecial7 = 0x3f,
    HpTypeFreighterShieldSpecial8 = 0x40,
    HpTypeFreighterShieldSpecial9 = 0x41,
    HpTypeFreighterShieldSpecial10 = 0x42
};
namespace Archetype
{
    struct ICliObjFactory;

    struct Root;
    struct EqObj;
    struct Ship;
    struct Solar;
    struct Equipment;
    struct LootCrate;
    struct AttachedEquipment;
    struct Launcher;
    struct Power;
    struct Engine;
    struct Armor;
    struct Shield;
    struct ShieldGenerator;
    struct Thruster;
    struct Gun;
    struct MineDropper;
    struct CounterMeasureDropper;
    struct Scanner;
    struct Light;
    struct Tractor;
    struct CargoPod;
    struct AttachedFXEquip;
    struct InternalFXEquip;
    struct RepairDroid;
    struct CloakingDevice;
    struct Commodity;
    struct RepairKit;
    struct ShieldBattery;
    struct TradeLaneEquip;
    struct Projectile;
    struct Munition;
    struct Mine;
    struct CounterMeasure;
    struct Asteroid;
    struct DynamicAsteroid;
    struct MotorData;
    struct Explosion;

    // Root vtable slot 4. Values from leaf get_class_type thunks (Common.dll.c).
    enum AClassType
    {
        AClassRoot = 0,
        AClassEqObj = 1,
        AClassSolar = 2,             // Archetype::Solar::get_class_type (Common.dll.c 0x71740, "return 2")
        AClassShip = 3,              // Archetype::Ship::get_class_type (Common.dll.c 0xc9a0, "return 3")
        AClassAsteroid = 4,          // Archetype::Asteroid::get_class_type (Common.dll.c 0xfd90, "return 4")
        AClassDynamicAsteroid = 5,   // Archetype::DynamicAsteroid::get_class_type (Common.dll.c 0xfe80, "return 5")
        AClassEquipment = 6,
        AClassAttachedEquipment = 7,
        AClassLootCrate = 8,
        AClassCargoPod = 9,
        AClassCommodity = 0xa,
        AClassPower = 0xb,
        AClassEngine = 0xc,
        AClassShield = 0xd,
        AClassShieldGenerator = 0xe,
        AClassThruster = 0xf,
        AClassLauncher = 0x10,
        AClassGun = 0x11,
        AClassMineDropper = 0x12,
        AClassCounterMeasureDropper = 0x13,
        AClassScanner = 0x14,
        AClassLight = 0x15,
        AClassTractor = 0x16,
        AClassAttachedFX = 0x17,
        AClassInternalFX = 0x18,
        AClassRepairDroid = 0x19,
        AClassRepairKit = 0x1a,
        AClassShieldBattery = 0x1b,
        AClassCloakingDevice = 0x1c,
        AClassTradeLane = 0x1d,
        AClassProjectile = 0x1e,
        AClassMunition = 0x1f,
        AClassMine = 0x20,
        AClassCounterMeasure = 0x21,
        AClassArmor = 0x22
    };

    // Callback for Enum* functions. Return true to continue enumeration.
    // ?EnumShips@Archetype@@YAXPAUArchEnumerator@1@@Z -- retail passes a callback OBJECT by
    // pointer, not a function pointer: 062f3329 loads its vtable and calls slot 0 as a thiscall
    // with the record as the only stack argument, and a false return stops the walk. The slot's
    // own name is not in the binary (nothing about ArchEnumerator is exported), so the port
    // names it Enumerate.
    struct ArchEnumerator
    {
        virtual bool Enumerate(Root* Record) = 0;
    };

    // ICliObj -- client archetype companion (Root+0x04). Retail Client\cmn\CliArch.cpp.
    // Vtable PTR_FUN_005c7734: 14 slots. Names 0-8 are the port catalog (no PE strings);
    // Slot09-13 keep SlotNN until a PE name is recovered. See docs/re/freelancer_cliobj.md.
    struct ICliObj
    {
        virtual void AddRef() = 0;                          // 0 +0x00  FUN_00406b50
        virtual void Release() = 0;                         // 1 +0x04  FUN_00406b60
        virtual bool ReadUnknown(INI_Reader* Reader) = 0;   // 2 +0x08
        virtual void Lod(INI_Reader* Reader) = 0;           // 3 +0x0C
        virtual void Redefine(ICliObj* Donor) = 0;          // 4 +0x10
        virtual void Bind(Root* Record) = 0;                // 5 +0x14
        virtual void FreeResources() = 0;                   // 6 +0x18  thunk → Slot11
        virtual void Inherit(ICliObj* Donor) = 0;           // 7 +0x1C
        virtual unsigned long Load(int LoadMode) = 0;       // 8 +0x20  LAB_00407380
        virtual void* Slot09(unsigned Flags) = 0;           // 9 +0x24  FUN_00406940 scalar dtor
        virtual void Slot10() = 0;                          // 10 +0x28 FUN_00407320
        virtual void Slot11() = 0;                          // 11 +0x2C FUN_00406d20
        virtual void Slot12(unsigned Arg) = 0;              // 12 +0x30 FUN_004074f0
        virtual void Slot13() = 0;                          // 13 +0x34 LAB_00407530
    };

    // Slot 2 on Root is redefine; these offsets are the *companion* vtable (Load*/Root).
    void CliObjLod(ICliObj* Companion, INI_Reader* Reader);           // slot 3
    void CliObjBind(ICliObj* Companion, Root* Record);                // slot 5
    void CliObjFreeResources(ICliObj* Companion);                     // slot 6
    void CliObjInherit(ICliObj* Companion, ICliObj* Donor);           // slot 7
    unsigned long CliObjLoad(ICliObj* Companion, int LoadMode);       // slot 8 +0x20

    // sizeof 0x5C. Phy map at +0x48 is VC6 std::map — opaque, not constructed.
    struct COMMON_API Root
    {
    public:
        explicit Root(ICliObj* Companion = nullptr);
        virtual ~Root();

        // Common.dll.txt:258697 (0x901f0, ??0Root@Archetype@@QAE@ABU01@@Z). User-declared in
        // retail: clears the three string members, builds an empty surfaces map, stamps the
        // vptr and then runs operator=. The leaf copy constructors (Armor 0xdcd0, Equipment
        // 0xd2c0, ...) are the compiler-generated memberwise ones that call it.
        Root(Root const& Other);

        // Common.dll.txt:258901-259123 (??4Root@Archetype@@QAEABU01@ABU01@@Z). CORRECTED
        // (D51, 2026-09-09): real retail logic, reachable from Archetype::LoadShips/LoadSolar's
        // "inherit=" donor-copy sequence via EqObj::operator= -- NOT dead code as a prior lane's
        // used_by_count=0 verdict concluded (that check used the corrupted decompiled .c's call
        // graph; the raw .txt XREF list shows 19 real callers). See tracker items 71875/71899
        // for the full XREF trail. Returns Root const& (not Root&) matching retail's mangled
        // return type.
        Root const& operator=(Root const& Other);

        // Slot 1. INI_Reader API is the ported text/BINI reader (common_ini.h).
        virtual bool read(INI_Reader& Reader);

        // Slot 2.
        virtual void redefine(Root const& Other);

        // Slot 3. Mesh via Archetype::Load(DaArchetype) → EngineIndex, then PhyArch::LoadSurfaces
        // unless ext is .sph. Increments LoadCount (retail +0x3C).
        virtual bool load();

        // Slot 4. Ghidra mislabels Root's thunk as RDLNode; map name is get_class_type.
        virtual AClassType get_class_type() const;

        bool is_loaded() const;
        long get_engine_index() const { return EngineIndex; }
        int get_script_index() const { return AnimIndex; }
        unsigned get_id() const { return Nickname.Id; }
        void AddRef();
        int Release();
        int GetRef() const;
        PhyArch::Part const* get_phy_part(unsigned PartId) const;
        // Listing 0x905d0. IEngine+0x5C(EngineIndex, IEngineInstance*).
        // Pass EngineObject* (stdcall IEngineInstance); nullptr = CNonPhys owned identity.
        // Mesh not loaded → FDUMP, -1.
        long create_instance(IEngineInstance* EngineInstance) const;

        ICliObj* Companion;                 // +0x04
        ID_String Nickname;                 // +0x08
        CacheString DaArchetype;            // +0x0C
        unsigned ObjectType;                // +0x10  OBJECT_TYPE bits
        int IdsName;                        // +0x14
        int IdsInfo;                        // +0x18
        float HitPts;                       // +0x1C  default 100
        float Mass;                         // +0x20  default 1
        ID_String ExplosionArch;            // +0x24
        float ExplosionResistance;          // +0x28  default 1
        float RotationInertiaX;             // +0x2C
        float RotationInertiaY;             // +0x30
        float RotationInertiaZ;             // +0x34
        bool bHasRotationInertia;           // +0x38
        bool bPhantomPhysics;               // +0x39
        int LoadCount;                      // +0x3C  AddRef/Release *and* load attempts
        long EngineIndex;                   // +0x40  -1 = not loaded
        int AnimIndex;                      // +0x44  -1
        // +0x48: the surfaces map PhyArch::LoadSurfaces fills (retail's export takes it as
        // std::map<unsigned int, PhyArch::Part>&). A modern std::map is smaller than retail's
        // 0x14-byte span here, so the pad keeps every derived archetype's offsets unchanged.
        std::map<unsigned int, PhyArch::Part> PhyParts;
        unsigned char PhyPartsPad[0x14 - sizeof(std::map<unsigned int, PhyArch::Part>)];

    protected:
        // Retail 0x90be0: distinct from load() -- returns false if not yet loaded (does NOT
        // call Load()/the engine mesh loader itself), otherwise (re)loads the .sur surfaces
        // map, skipped for a .sph mesh path.
        bool init_physical_representation();

        // Slot 5.
        virtual void free_resources();
        // Retail 0x90c90: walks/frees the +0x48 surfaces map directly (does not touch
        // EngineIndex/AnimIndex/Companion like free_resources() does).
        void free_physical_representation();
    };

    static_assert(sizeof(Root) == 0x5C, "Root sizeof 0x5C");
    static_assert(offsetof(Root, Companion) == 0x04, "Root+0x04 Companion");
    static_assert(offsetof(Root, Nickname) == 0x08, "Root+0x08 nickname");
    static_assert(offsetof(Root, DaArchetype) == 0x0C, "Root+0x0C DA_archetype");
    static_assert(offsetof(Root, ObjectType) == 0x10, "Root+0x10 type bits");
    static_assert(offsetof(Root, IdsName) == 0x14, "Root+0x14 ids_name");
    static_assert(offsetof(Root, IdsInfo) == 0x18, "Root+0x18 ids_info");
    static_assert(offsetof(Root, HitPts) == 0x1C, "Root+0x1C hit_pts");
    static_assert(offsetof(Root, Mass) == 0x20, "Root+0x20 mass");
    static_assert(offsetof(Root, ExplosionArch) == 0x24, "Root+0x24 explosion_arch");
    static_assert(offsetof(Root, ExplosionResistance) == 0x28, "Root+0x28 explosion_resistance");
    static_assert(offsetof(Root, LoadCount) == 0x3C, "Root+0x3C load/ref count");
    static_assert(offsetof(Root, EngineIndex) == 0x40, "Root+0x40 engine index");

    // Archetype::ShieldLink. Listing shield_link element stride 0x1c: ID + EquipId + CacheString +
    // VC6 vector<CacheString>. Port nested GeneratorHps is MSVC vector (not 0x10 allocator+3ptr).
    // Heap vector* at EqObj+0x68. Retail exports the default ctor (0xc6c0), and the memberwise
    // copy ctor (0xc700), dtor (0xc930) and operator= (0xc780) -- the latter three stay implicit.
    struct COMMON_API ShieldLink
    {
        // 0xc6c0: ShieldArch.clear(), EquipId = SUBOBJ_ID_NONE, Hardpoint.clear(), empty vector.
        ShieldLink();
        ShieldLink& operator=(ShieldLink const&) = default;

        ID_String ShieldArch;                 // value 0 CreateID
        unsigned short EquipId;               // ShieldIdMaker CreateShieldID
        unsigned short Pad06;
        CacheString Hardpoint;                 // value 1 -- CEShield::Connect
        std::vector<CacheString> GeneratorHps; // values 2+ generator hardpoints
    };

    // Archetype::DamageObjInfo -- CollisionGroup dmg_hp/dmg_obj pair. Listing VC6 list node +8
    // CacheString / +0xc ID.
    struct COMMON_API DamageObjInfo
    {
        // 0xf8a0: Hardpoint (CacheString) clear, then Arch.clear().
        DamageObjInfo();
        // 0x20ee0 (ICF-shared with operator=): copies both dwords. User-provided because retail
        // exports it, and an implicit trivial copy ctor would not be exported.
        DamageObjInfo(DamageObjInfo const& Other);
        DamageObjInfo& operator=(DamageObjInfo const&) = default;

        CacheString Hardpoint;
        ID_String Arch;

        // Common.dll.c 23240-23245 (0xf840): unsigned compare on the Arch id only
        // (Hardpoint is not compared).
        bool operator<(DamageObjInfo const& Other) const;
        // Common.dll.c 23228-23233 (0xf820): Arch id equality only (Hardpoint not compared).
        bool operator==(DamageObjInfo const& Other) const;
        // Common.dll.c 23252-23257 (0xf860): Arch id inequality only (Hardpoint not compared).
        bool operator!=(DamageObjInfo const& Other) const;
        // Common.dll.c 23264-23273 (0xf880): unsigned greater-than on the Arch id only.
        bool operator>(DamageObjInfo const& Other) const;
    };

    // fuse element = CreateID + two floats. Shared shape for both CollisionGroup's own fuse
    // list (CollisionGroup::Fuses, ReadCollisionGroup) and EqObj's separate +0x74 fuse list
    // (EqObj::Fuses, EqObj::read's "fuse" case, D46) -- retail parses "fuse" identically in
    // both contexts, confirmed by direct trace of each call site.
    struct FGroupFuse
    {
        ID_String Fuse;
        float Value1;
        float Value2;
    };

    // Archetype::DamageObjInfoList -- retail's list of DamageObjInfo (a class, per the mangling:
    // ABVDamageObjInfoList@Archetype@@). Three dwords like a VC6 std::list {allocator byte, head
    // node*, size}; the port keeps a heap vector* where the head node pointer sits. CollisionGroup
    // holds two of them (+0x40 / +0x4c), which CArchGroup::ParentDamageObjs / GroupDamageObjs
    // hand out.
    // Retail's special members are the std::list ones ICF-folded with list<CollisionGroupDesc>:
    // ctor 0xf8f0, copy ctor 0xf920, operator= 0xf9b0, dtor 0x468e0.
    class COMMON_API DamageObjInfoList
    {
    public:
        // 0xf8f0: allocator byte, fresh empty head node at +0x04, size 0.
        DamageObjInfoList();
        // 0xf920: fresh head, then one node appended per element of Other, size counted up.
        DamageObjInfoList(DamageObjInfoList const& Other);
        // 0x468e0: frees every node and the head, zeroes head and size.
        ~DamageObjInfoList();
        // 0xf9b0: self-assignment is a no-op; otherwise the existing nodes are overwritten in
        // order, surplus nodes erased, missing ones appended -- i.e. element-wise assignment.
        DamageObjInfoList& operator=(DamageObjInfoList const& Other);

        // Common.dll.c 23292-23307 (0xf8c0, ?IsValid@DamageObjInfoList@Archetype@@QAE_NXZ,
        // non-const). Retail walks the raw VC6 list checking every node's Hardpoint string is
        // non-empty and its Arch id's low 30 bits are nonzero; false as soon as either fails,
        // true for an empty list. Ported directly over the vector representation used here.
        bool IsValid();

        unsigned char Pad0[4];               // +0x00  VC6 list allocator byte (unused by the port)
        std::vector<DamageObjInfo>* Items;   // +0x04  retail head node*
        unsigned Count;                      // +0x08
    };
    static_assert(sizeof(DamageObjInfoList) == 0x0c, "DamageObjInfoList sizeof 0xc");

    // Archetype::FuseIgnitionList -- same embedded-list pattern as DamageObjInfoList, but over
    // FGroupFuse: CollisionGroup::Fuses at +0x58 (CArchGroup::FuseIgnitions).
    // A class in retail: ?FuseIgnitions@CArchGroup@@QBEABVFuseIgnitionList@Archetype@@XZ.
    // Retail exports no special members for it; CollisionGroup's own (0x62f9950 ctor, 0xfa70
    // copy ctor, 0xfbc0 operator=) build, copy and assign the list inline exactly as
    // DamageObjInfoList's do, so the port gives it the same four members.
    class FuseIgnitionList
    {
    public:
        FuseIgnitionList();
        FuseIgnitionList(FuseIgnitionList const& Other);
        ~FuseIgnitionList();
        FuseIgnitionList& operator=(FuseIgnitionList const& Other);

        unsigned char Pad0[4];             // +0x00  VC6 list allocator byte (unused by the port)
        std::vector<FGroupFuse>* Items;    // +0x04  retail head node*
        unsigned Count;                    // +0x08
    };
    static_assert(sizeof(FuseIgnitionList) == 0x0c, "FuseIgnitionList sizeof 0xc");

    // sizeof 0x6c. operator_new(0x6c) FUN_062f9b10. Next* at +0. The three VC6 lists at
    // +0x40/+0x4c/+0x58 are the list classes above (a heap vector* in each head-node slot).
    struct COMMON_API CollisionGroup
    {
        CollisionGroup();
        CollisionGroup(CollisionGroup const& Other);
        ~CollisionGroup();
        CollisionGroup& operator=(CollisionGroup const& Other);

        CollisionGroup* Next;                         // +0x00
        unsigned short Id;                            // +0x04 CollGroupIdMaker
        // +0x06, +0x17 and +0x25..+0x27 are alignment padding: no retail ctor, copy or
        // operator= (062f9930 / 0626fa70 / 0626fbc0) writes them and nothing reads them.
        char const* ObjName;                          // +0x08 obj
        unsigned Type;                                // +0x0C default 1
        int HitPts;                                   // +0x10
        bool bSeparable;                              // +0x14
        bool bDestroyParent;                          // +0x15
        bool bRootHealthProxy;                        // +0x16
        float ParentImpulse;                          // +0x18
        float ChildImpulse;                           // +0x1C
        float Mass;                                   // +0x20 default 1
        bool bHasRotationInertia;                     // +0x24
        float RotationInertiaX;                       // +0x28
        float RotationInertiaY;                       // +0x2C
        float RotationInertiaZ;                       // +0x30
        ID_String SeparationExplosion;                // +0x34
        ID_String DebrisType;                         // +0x38
        float ExplosionResistance;                    // +0x3C default 1
        DamageObjInfoList DmgObjs;                    // +0x40 dmg_hp / dmg_obj (ParentDamageObjs)
        DamageObjInfoList GroupDmgObjs;               // +0x4C group_dmg_hp / group_dmg_obj
        FuseIgnitionList Fuses;                       // +0x58 fuse
        unsigned LinkedEquipType;                     // +0x64
        float LinkedEquipHealth;                      // +0x68
    };

    static_assert(sizeof(CollisionGroup) == 0x6c, "CollisionGroup sizeof 0x6c");
    static_assert(offsetof(CollisionGroup, Id) == 0x04, "CollisionGroup+0x04 id");
    static_assert(offsetof(CollisionGroup, ObjName) == 0x08, "CollisionGroup+0x08 obj");
    static_assert(offsetof(CollisionGroup, HitPts) == 0x10, "CollisionGroup+0x10 hit_pts");
    static_assert(offsetof(CollisionGroup, ParentImpulse) == 0x18, "CollisionGroup+0x18 parent_impulse");
    static_assert(offsetof(CollisionGroup, RotationInertiaX) == 0x28, "CollisionGroup+0x28 rotation_inertia");
    static_assert(offsetof(CollisionGroup, DmgObjs) == 0x40, "CollisionGroup+0x40 dmg list");
    static_assert(offsetof(CollisionGroup, GroupDmgObjs) == 0x4c, "CollisionGroup+0x4c group dmg list");
    static_assert(offsetof(CollisionGroup, Fuses) == 0x58, "CollisionGroup+0x58 fuse list");
    static_assert(offsetof(CollisionGroup, LinkedEquipType) == 0x64, "CollisionGroup+0x64 linked_equip");

    // FUN_062f9b10. Appends to EqObj+0x5C. LoadShips/LoadSolar [CollisionGroup] only.
    void ReadCollisionGroup(INI_Reader& Reader, EqObj* Owner, unsigned short GroupId);

    // sizeof 0x94. fuse stays swallowed (init does not read). docking_sphere is parsed into
    // DockingSpheres (see that struct). CollisionGroup parsed.
    struct COMMON_API EqObj : public Root
    {
    public:
        explicit EqObj(ICliObj* Companion = nullptr);
        // Common.dll.txt 0x91460 (??0EqObj@Archetype@@QAE@ABU01@@Z): user-declared in retail --
        // default-constructs the base and the containers, then runs operator=.
        EqObj(EqObj const& Other);
        virtual ~EqObj();

        // Common.dll.txt:261602-261819 (??4EqObj@Archetype@@QAEABU01@ABU01@@Z). CORRECTED
        // (D51, 2026-09-09): real retail logic backing Ship/Solar "inherit=" (Archetype::
        // LoadShips 0x062f2d43 / Archetype::LoadSolar 0x062f383d), not dead code -- see
        // tracker item 71899's notes. Field trace against raw disasm: calls Root::operator=
        // for the base part, deep-clones the CollisionGroups singly-linked list (heap-alloc +
        // CollisionGroup copy-ctor per node, matching retail's node-by-node relink loop) and
        // assigns DockingSpheres (plain std::vector assignment subsumes retail's inlined
        // vector-operator= helpers). Deliberately does NOT touch ShieldIdNext/ShieldLinks/
        // Fuses/bNomad/bDockingCamera -- confirmed by direct disasm read that retail's real
        // body never references those offsets (+0x60/+0x68/+0x74/+0x80/+0x81) at all, so they
        // are not a missed copy, they are retail's own actual (partial) behavior. NOT a general-
        // purpose deep-copy: like retail, does not free/merge into a non-empty destination
        // CollisionGroups chain (both known callers assign into freshly-constructed objects).
        // Returns EqObj const&, matching retail's mangled return type. Explicitly declared here
        // (rather than left implicit) because Root::operator= now being real would otherwise
        // make the compiler synthesize an unsafe memberwise EqObj::operator= that shallow-
        // copies CollisionGroups/ShieldLinks/Fuses pointers.
        EqObj const& operator=(EqObj const& Other);

        virtual bool read(INI_Reader& Reader);
        // 0x91c60: a single `JMP Archetype::Root::redefine`.
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;

        CollisionGroup const* get_group_by_id(unsigned short GroupId) const;
        CollisionGroup const* get_group_by_name(CacheString const& Name) const;
        bool traverse_groups(CollisionGroup const*& Cursor) const;
        bool dock_terminal_valid(int Index) const;
        // Common.dll.c 0x91f00. Archetype-level (not per-instance) group list: clears
        // OutDescs then appends one CollisionGroupDesc per CollisionGroup in the +0x5C
        // chain, each with Health forced to 1.0f -- the archetype has no damage state, so
        // "undamaged" just means every group at full health. See CEqObj::get_collision_
        // group_description (common_cobject.h) for the per-instance, real-health version.
        bool get_undamaged_collision_group_list(std::list<CollisionGroupDesc>& OutDescs) const;

        CollisionGroup* CollisionGroups;              // +0x5C linked CollisionGroup*
        unsigned short ShieldIdNext;              // +0x60 ShieldIdMaker, ctor 0xfff0
        unsigned char Pad62[6];                   // +0x62..+0x67 VC6 vector allocator
        std::vector<ShieldLink>* ShieldLinks;    // +0x68 heap; retail begin/end/cap
        unsigned char Pad6c[8];                   // +0x6c/+0x70 unused (retail end/cap)
        // +0x74 fuse list. Archetype::Ship::Ship's raw disassembly (Common.dll.c 126553-126567,
        // FUN_062f2180, item 1932) shows the real retail construction shape: byte[0] is set from
        // an uninitialized-in-source value (the compiler happened to fill it from the high byte
        // of the `this` pointer -- not meaningful data, matches this codebase's established
        // "harmless uninitialized field" precedent), then a heap 0x14-byte circular-list
        // sentinel is allocated (self-referential Next=Prev=self, the VC6 std::list default
        // ctor, as for CSolar::ContinualAnims) and stored at +4 (Head), with +8 (Count) set to 0.
        // CORRECTED (D46): EqObj::read DOES populate this list -- Common.dll.c 126077-126091
        // (?read@EqObj@Archetype@@UAE_NAAVINI_Reader@@@Z, item 12866) handles a "fuse" INI
        // value by CreateID'ing value(0) and reading value(1)/value(2) as two floats into a
        // freshly list-inserted node, exactly the same {ID_String Fuse; float Value1; float
        // Value2;} element shape already modeled as Archetype::FGroupFuse for
        // CollisionGroup::Fuses (see ReadCollisionGroup's own "fuse" handling, common_
        // archetype.cpp) -- so this is a real, previously-mislabeled gap, not a genuinely-
        // discarded field. Ported the same way as ShieldLinks just above: a heap
        // std::vector<FGroupFuse>* in the first 4 bytes of the old 0x0C-byte slot (this
        // build's std::vector is 16 bytes, too wide to embed by value here without shifting
        // the retail-matching +0x80/+0x81 offsets of bNomad/bDockingCamera below -- same
        // constraint documented for DockingSpheres/VibeValue, avoided here by using a pointer
        // like ShieldLinks/CollisionGroup::Fuses already do instead of growing the struct).
        // CONSUMER: freelancer.exe FUN_00533e10 (EquippedObjScanFuseIgnitions,
        // client_equip_fx.cpp) walks this list and CollisionGroup::Fuses, compares HitPts
        // against the Fuse{Id,Value1,Value2} thresholds, ignites the named fuse through
        // FUN_00534240 (a copy of the exe's ClientFuse prototype, client_fuse.h) and
        // Fuse::Load's the prototype. Common.dll's Fuse/FuseAction (common_fuses.h) are the
        // runtime base classes; Server.dll's own fuse database (FUN_06cfd4b0) is not ported.
        std::vector<FGroupFuse>* Fuses;           // +0x74 heap (retail VC6 list dummy slot)
        unsigned char Pad78[8];                   // +0x78 unused (retail Head/Count remainder)
        bool bNomad;                              // +0x80
        bool bDockingCamera;                      // +0x81 default true
        unsigned char Pad82[2];                   // Alignment pad
        struct DockingSphere
        {
            // Retail layout (0x10-byte element pushed by FUN_062fc710 from the stack record
            // EqObj::read builds at .text:062f1898..062f1938): float Radius (+0x00) = value(2);
            // char* HardpointName (+0x04) = value(1), StringAlloc'd then _strlwr'd (e.g.
            // "dockmount01"); char* AnimName (+0x08) = optional value(3), StringAlloc'd (the
            // dock animation script, e.g. "Sc_open dock4"; cleared when absent); unsigned
            // Type (+0x0C) = value(0) looked up in the PTR_s_berth_063eca00 8-entry table
            // (berth=1, moor_small=3, moor_medium=4, moor_large=5, ring=6, pad=6, jump=7,
            // airlock=8; no match -> 0).
            // Readers: +0x04 -- CEqObj::launch_pos (HardpointPosition, .text:062aa9d5) and
            // Server pub::SpaceObj::GetDockingTarget (.text:06cf9978); +0x08 --
            // CEqObj::init_docking_points (PlayScript, .text:062aaeb4); +0x0C -- Server.
            float Radius;
            std::string HardpointName;
            std::string AnimName;
            unsigned Type;
        };
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        // Release: a modern std::vector is three pointers (0xC), so this dword stands where
        // retail's VC6 allocator word is (+0x84) and the vector's first/last/end land on
        // retail's +0x88/+0x8c/+0x90. With iterator debugging the vector carries its own
        // proxy pointer in that dword and is 0x10 itself. Either way EqObj ends at 0x94 and
        // the Solar / Ship offsets below hold in every configuration.
        unsigned char DockingSpheresPad[4];         // +0x84 (Release only)
#endif
        std::vector<DockingSphere> DockingSpheres; // +0x84 (Debug) / +0x88 (Release)
    };

    static_assert(sizeof(EqObj) == 0x94, "EqObj sizeof 0x94");
    static_assert(offsetof(EqObj, CollisionGroups) == 0x5C, "EqObj+0x5C CollisionGroup*");
    static_assert(offsetof(EqObj, ShieldIdNext) == 0x60, "EqObj+0x60 ShieldIdMaker");
    static_assert(offsetof(EqObj, ShieldLinks) == 0x68, "EqObj+0x68 shield_link");
    static_assert(offsetof(EqObj, bNomad) == 0x80, "EqObj+0x80 nomad");
    static_assert(offsetof(EqObj, bDockingCamera) == 0x81, "EqObj+0x81 docking_camera");

    // Thin: extra fields from Solar ctor 0x93380 / read 0x93470. sizeof 0xAC.
    struct COMMON_API Solar : public EqObj
    {
    public:
        explicit Solar(ICliObj* Companion = nullptr);
        virtual ~Solar();

        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;


        bool bDestructible;       // +0x94
        float SolarRadius;        // +0x98 default 500
        int Toughness;            // +0x9C
        CacheString OpenAnim;     // +0xA0
        CacheString JumpOutHp;    // +0xA4
        ID_String Loadout;        // +0xA8
    };

    static_assert(sizeof(Solar) == 0xAC, "Solar sizeof 0xAC");
    static_assert(offsetof(Solar, bDestructible) == 0x94, "Solar+0x94 destructible");
    static_assert(offsetof(Solar, SolarRadius) == 0x98, "Solar+0x98 solar_radius");
    static_assert(offsetof(Solar, Loadout) == 0xA8, "Solar+0xA8 loadout");

    // One element of retail's Ship+0x130 vector -- 0x14 bytes: the HpAttachmentType at +0x00 and a
    // VC6 std::vector<CacheString> at +0x04 (Common.dll .text:062f29b8 strides by 0x14, .text:062f29c4
    // returns element+0x04). The port keeps the same two members with a modern vector.
    struct LegalHpTypes
    {
        LegalHpTypes() : Type(HpTypeNone) {}

        HpAttachmentType Type;              // +0x00
        std::vector<CacheString> Hardpoints; // +0x04 (retail: 0x10-byte VC6 vector)
    };

    // EqObj + CommReferrable (at +0x94) + named scalars. Retail exports both tables:
    // ??_7Ship@Archetype@@6BEqObj@1@@ and ??_7Ship@Archetype@@6BCommReferrable@@@ (.rdata:06399828,
    // HasVoiceInfo/GetMsgIdPrefix/read_comm_ini_data). Ship::read reaches msg_id_prefix through the
    // CommReferrable subobject's slot 2 (.text:062f2531: `LEA ECX,[ESI+0x94]; CALL [EAX+8]`), and
    // Server.dll pub::TranslateShipToMsgIdPrefix calls its slot 1 at +0x94 (.text:06d5e0e8).
    struct COMMON_API Ship : public EqObj, public CommReferrable
    {
    public:
        explicit Ship(ICliObj* Companion = nullptr);
        virtual ~Ship();

        // .rdata:063a08ac, ?MAX_EXHAUST_NOZZLES@Ship@Archetype@@2HB = 9.
        static int const MAX_EXHAUST_NOZZLES;

        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;


        // 0x929a0 ?get_legal_hps@Ship@Archetype@@QBEPBV?$vector@UCacheString@@V?$allocator@UCacheString@@@std@@@std@@W4HpAttachmentType@@@Z
        // (.text:062f29a0): linear scan of the hp_type table for Type, null when the hull declares
        // no hardpoint of that class. Server.dll imports it (IAT .rdata:06d64954) for the
        // "equipment exceeds ship specs" cheat check and for .text:06d5fa41.
        std::vector<CacheString> const* get_legal_hps(HpAttachmentType Type) const;

        CacheString BayDoorAnim;                // +0xDC
        CacheString HpBaySurface;               // +0xE0
        CacheString HpBayExternal;              // +0xE4
        CacheString HpTractorSource;            // +0xE8
        int IdsInfo1;                           // +0xEC
        int IdsInfo2;                           // +0xF0
        int IdsInfo3;                           // +0xF4
        int ShipClass;                          // +0xF8
        int NumExhaustNozzles;                  // +0xFC
        float HoldSize;                         // +0x100
        float LinearDrag;                       // +0x104
        float AngularDragX;                     // +0x108
        float AngularDragY;                     // +0x10C
        float AngularDragZ;                     // +0x110
        float SteeringTorqueX;                  // +0x114
        float SteeringTorqueY;                  // +0x118
        float SteeringTorqueZ;                  // +0x11C
        float NudgeForce;                       // +0x120
        float StrafeForce;                      // +0x124
        float StrafePowerUsage;                 // +0x128
        float MaxBankAngle;                     // +0x12C radians

        // Retail +0x130: a VC6 std::vector of 0x14-byte {HpAttachmentType; vector<CacheString>}
        // records -- Ship::get_legal_hps (.text:062f29a0) walks [this+0x134,this+0x138) with a 0x14
        // stride comparing element+0x00 and returns element+0x04. Filled by the "hp_type" INI
        // directive (.text:062f2549). Same member, same place in the declaration order, as a
        // modern std::vector (no retail ABI): its size differs from VC6's 0x10 and between Debug
        // and Release, so the two members after it are no longer at retail's +0x140/+0x144 and
        // neither they nor sizeof(Ship) are asserted. Every access is by name.
        std::vector<LegalHpTypes> LegalHps;     // retail +0x130
        int NanobotLimit;                       // retail +0x140
        int ShieldBatteryLimit;                 // retail +0x144
    };

    static_assert(offsetof(Ship, BayDoorAnim) == 0xDC, "Ship+0xDC bay_door_anim");
    static_assert(offsetof(Ship, HoldSize) == 0x100, "Ship+0x100 hold_size");
    static_assert(offsetof(Ship, MaxBankAngle) == 0x12C, "Ship+0x12C max_bank_angle");

    // Thin: Root + equipment scalars. sizeof 0x70. Leaf classes for alloc_equip switch.
    struct COMMON_API Equipment : public Root
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Equipment& operator=(Equipment const&) = default;

        explicit Equipment(ICliObj* Companion = nullptr);

        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        // Base default 0 (unnamed HpAttachmentType value). Overridden by many leaves.
        virtual HpAttachmentType get_hp_type() const;
        // Slots 7-9 of retail's Equipment vftable (.rdata:0639988c..06399894).
        // Slot 7 (+0x1c). Base 0x1260 (ICF body `MOV EAX,ECX; RET`): the item is its own loot.
        virtual Equipment* get_loot_appearance();
        // Slot 8 (+0x20). Base 0xc5390 (ICF body `XOR EAX,EAX; RET`): no cargo pod.
        virtual CargoPod* get_pod_appearance();
        // Slot 9 (+0x24). Base 0x3ba00 (ICF body `XOR AL,AL; RET`); AttachedEquipment says true.
        virtual bool is_attached() const;

        float Volume;                 // +0x5C
        bool bUseCount;               // +0x60 default true
        int UnitsPerContainer;        // +0x64 default 1
        ID_String TractoredExplosion; // +0x68 tractored_explosion
        bool bLootable;               // +0x6C lootable, default true
    };

    static_assert(sizeof(Equipment) == 0x70, "Equipment sizeof 0x70");
    static_assert(offsetof(Equipment, Volume) == 0x5C, "Equipment+0x5C volume");
    static_assert(offsetof(Equipment, bUseCount) == 0x60, "Equipment+0x60 use_count");
    static_assert(offsetof(Equipment, UnitsPerContainer) == 0x64, "Equipment+0x64 units_per_container");
    static_assert(offsetof(Equipment, TractoredExplosion) == 0x68, "Equipment+0x68 tractored_explosion");
    static_assert(offsetof(Equipment, bLootable) == 0x6C, "Equipment+0x6C lootable");

    // sizeof 0x70 same as Equipment. listing ctor 0x94460 / LoadEquipment 0x97c50:
    // Equipment::Equipment then LootCrate vtable; get_class_type 8. No read/load override.
    struct COMMON_API LootCrate : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        LootCrate& operator=(LootCrate const&) = default;

        explicit LootCrate(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
    };

    static_assert(sizeof(LootCrate) == 0x70, "LootCrate sizeof 0x70");

    // sizeof 0x88. LoadEquipment operator_new / ctor 0x95310. listing AttachedEquipment::read 0x953b0.
    struct COMMON_API AttachedEquipment : public Equipment
    {
    public:
        explicit AttachedEquipment(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual bool is_attached() const;

        // Retail Archetype::AttachedEquipment::operator= (Common.dll.c 127882/127910/127923/
        // 127929, one of the "inherit=" shape-copy family driven by Equipment::InheritFrom).
        // Not virtual, no traced retail call site of its own besides FUN_062f3ea0's dispatch.
        AttachedEquipment& operator=(AttachedEquipment const& Other);

        ID_String SeparationExplosion; // +0x70
        ID_String DebrisType;          // +0x74
        float ChildImpulse;            // +0x78
        float ParentImpulse;           // +0x7c
        CacheString HpChild;           // +0x80  INI HP_child string; not engine bind
        int Toughness;                 // +0x84
    };

    static_assert(sizeof(AttachedEquipment) == 0x88, "AttachedEquipment sizeof 0x88");
    static_assert(offsetof(AttachedEquipment, SeparationExplosion) == 0x70,
                  "AttachedEquipment+0x70 separation_explosion");
    static_assert(offsetof(AttachedEquipment, HpChild) == 0x80, "AttachedEquipment+0x80 HP_child");
    static_assert(offsetof(AttachedEquipment, Toughness) == 0x84, "AttachedEquipment+0x84 toughness");

    // sizeof 0xa0. ctor 0x96010. Parent of Gun / MineDropper / CMDropper. listing read 0x960f0.
    struct COMMON_API Launcher : public AttachedEquipment
    {
    public:
        explicit Launcher(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;

        // Retail Archetype::Launcher::operator= (Common.dll.c 127939/127957/127960), part of
        // the "inherit=" shape-copy family; see Equipment::InheritFrom.
        Launcher& operator=(Launcher const& Other);

        float DamagePerFire;              // +0x88
        float PowerUsage;                 // +0x8c
        float RefireDelay;                // +0x90
        float MuzzleVelocity;             // +0x94
        ID_String ProjectileArchetype;    // +0x98  CELauncher GetEquipment
        CacheString UseAnimation;         // +0x9c
    };

    static_assert(sizeof(Launcher) == 0xa0, "Launcher sizeof 0xa0");
    static_assert(offsetof(Launcher, DamagePerFire) == 0x88, "Launcher+0x88 damage_per_fire");
    static_assert(offsetof(Launcher, ProjectileArchetype) == 0x98,
                  "Launcher+0x98 projectile_archetype");
    static_assert(offsetof(Launcher, UseAnimation) == 0x9c, "Launcher+0x9c use_animation");

    // sizeof 0x80. CEPower reads Capacity @ +0x70 / ThrustCapacity @ +0x78.
    struct COMMON_API Power : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Power& operator=(Power const&) = default;

        explicit Power(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float Capacity;            // +0x70
        float ChargeRate;          // +0x74
        float ThrustCapacity;      // +0x78
        float ThrustChargeRate;    // +0x7c
    };

    static_assert(sizeof(Power) == 0x80, "Power sizeof 0x80");
    static_assert(offsetof(Power, Capacity) == 0x70, "Power+0x70 capacity");
    static_assert(offsetof(Power, ThrustCapacity) == 0x78, "Power+0x78 thrust_capacity");

    struct COMMON_API Engine : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Engine& operator=(Engine const&) = default;

        explicit Engine(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        // Field names/offsets verified against Common.dll.c ~128973 (Engine::read, retail
        // 0x950b0): max_force->+0x70, linear_drag->+0x74, power_usage->+0x78,
        // cruise_power_usage->+0x7c, cruise_charge_time->+0x80, indestructible->+0x84,
        // reverse_fraction->+0x88.
        float ThrustMultiplier;           // +0x70  max_force; GetThrust multiplies by this
        float LinearDrag;                 // +0x74  GetLinearDrag returns this; GetCruiseDrag adds CRUISE_DRAG
        float PowerDrawFactor;            // +0x78  power_usage; GetPowerDraw multiplies abs(throttle) by this
        float CruisePowerUsage;           // +0x7c  cruise_power_usage; GetCruisePowerDraw returns this
        float CruiseChargeTime;           // +0x80  cruise_charge_time; EngageCruise seeds the charge timer
        bool bIndestructible;             // +0x84  indestructible
        unsigned char Pad85[3];
        float ReverseFraction;            // +0x88  reverse_fraction; GetThrust uses ONLY when throttle < 0
    };

    static_assert(sizeof(Engine) == 0x8c, "Engine sizeof 0x8c");
    static_assert(offsetof(Engine, ThrustMultiplier) == 0x70, "Engine+0x70 max_force");
    static_assert(offsetof(Engine, LinearDrag) == 0x74, "Engine+0x74 linear_drag");
    static_assert(offsetof(Engine, PowerDrawFactor) == 0x78, "Engine+0x78 power_usage");
    static_assert(offsetof(Engine, CruisePowerUsage) == 0x7c, "Engine+0x7c cruise_power_usage");
    static_assert(offsetof(Engine, CruiseChargeTime) == 0x80, "Engine+0x80 cruise_charge_time");
    static_assert(offsetof(Engine, bIndestructible) == 0x84, "Engine+0x84 indestructible");
    static_assert(offsetof(Engine, ReverseFraction) == 0x88, "Engine+0x88 reverse_fraction");

    struct COMMON_API Armor : public Equipment
    {
    public:
        explicit Armor(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        // Retail Archetype::Armor::operator= (Common.dll.c 127978/127981/127984), part of the
        // "inherit=" shape-copy family -- reused verbatim for RepairDroid/RepairKit/
        // ShieldBattery too (same Equipment+1-field shape); see Equipment::InheritFrom.
        Armor& operator=(Armor const& Other);

        float HitPtsScale;   // +0x70  hit_pts_scale, default 1
    };

    static_assert(sizeof(Armor) == 0x74, "Armor sizeof 0x74");
    static_assert(offsetof(Armor, HitPtsScale) == 0x70, "Armor+0x70 hit_pts_scale");

    struct COMMON_API Shield : public AttachedEquipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Shield& operator=(Shield const&) = default;

        explicit Shield(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
    };

    static_assert(sizeof(Shield) == 0x88, "Shield sizeof 0x88");

    struct COMMON_API ShieldGenerator : public AttachedEquipment
    {
    public:
        explicit ShieldGenerator(ICliObj* Companion = nullptr);
        // 0xe240 (??0ShieldGenerator@Archetype@@QAE@ABU01@@Z): memberwise, the hp_shield_type
        // array copied into a block of exactly its size (retail's VC6 vector copy ctor).
        ShieldGenerator(ShieldGenerator const& Other);
        // 0xe3c0: Root::operator= and every scalar inline, then the VC6 vector operator= on
        // the hp_shield_type array (capacity kept when it suffices).
        ShieldGenerator& operator=(ShieldGenerator const& Other);
        virtual ~ShieldGenerator();
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        int get_number_of_hp_types() const;
        HpAttachmentType get_hp_type_by_index(int Index) const;

        unsigned HpType;              // +0x88  hp_type (single), default 5 = HpTypeShieldGenerator
        ID_String ShieldType;         // +0x8c  shield_type
        float RegenerationRate;       // +0x90
        float MaxCapacity;            // +0x94
        float ConstantPowerDraw;      // +0x98
        float RebuildPowerDraw;       // +0x9c
        float OfflineThreshold;       // +0xa0
        float OfflineRebuildTime;     // +0xa4
        // Retail's ctor copies the low byte of its ICliObj* companion parameter here --
        // uninitialized-stack-reuse decompiler noise, not real data. Always zero in the port.
        unsigned char PadA8[4];
        // +0xac/+0xb0/+0xb4: malloc'd [begin,end,cap) array of hp_shield_type ids (retail
        // VC6 vector<unsigned>, doubling growth). ~ShieldGenerator frees HpShieldTypesBegin.
        unsigned* HpShieldTypesBegin;  // +0xac
        unsigned* HpShieldTypesEnd;    // +0xb0
        unsigned* HpShieldTypesCap;    // +0xb4
    };

    static_assert(sizeof(ShieldGenerator) == 0xb8, "ShieldGenerator sizeof 0xb8");
    static_assert(offsetof(ShieldGenerator, HpType) == 0x88, "ShieldGenerator+0x88 hp_type");
    static_assert(offsetof(ShieldGenerator, ShieldType) == 0x8c, "ShieldGenerator+0x8c shield_type");
    static_assert(offsetof(ShieldGenerator, RegenerationRate) == 0x90,
                  "ShieldGenerator+0x90 regeneration_rate");
    static_assert(offsetof(ShieldGenerator, OfflineRebuildTime) == 0xa4,
                  "ShieldGenerator+0xa4 offline_rebuild_time");
    static_assert(offsetof(ShieldGenerator, HpShieldTypesBegin) == 0xac,
                  "ShieldGenerator+0xac hp_shield_type array begin");
    static_assert(offsetof(ShieldGenerator, HpShieldTypesEnd) == 0xb0,
                  "ShieldGenerator+0xb0 hp_shield_type array end");
    static_assert(offsetof(ShieldGenerator, HpShieldTypesCap) == 0xb4,
                  "ShieldGenerator+0xb4 hp_shield_type array cap");

    struct COMMON_API Thruster : public AttachedEquipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Thruster& operator=(Thruster const&) = default;

        explicit Thruster(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual HpAttachmentType get_hp_type() const;

        float PowerUsage; // +0x88
        float MaxThrust;  // +0x8C
    };

    static_assert(sizeof(Thruster) == 0x90, "Thruster sizeof 0x90");
    static_assert(offsetof(Thruster, PowerUsage) == 0x88, "Thruster+0x88 power");
    static_assert(offsetof(Thruster, MaxThrust) == 0x8C, "Thruster+0x8C thrust");

    struct COMMON_API Gun : public Launcher
    {
    public:
        explicit Gun(ICliObj* Companion = nullptr);
        // 0xea00 (??0Gun@Archetype@@QAE@ABU01@@Z): memberwise, the hp_gun_type array copied
        // into a block of exactly its size (retail's VC6 vector copy ctor).
        Gun(Gun const& Other);
        // 0xebe0: Root::operator= and every scalar inline, then the VC6 vector operator= on the
        // hp_gun_type array (capacity kept when it suffices), then +0xcc / +0xd0.
        Gun& operator=(Gun const& Other);
        ~Gun();
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;

        virtual HpAttachmentType get_hp_type() const;

        float get_munition_range() const;
        int get_number_of_hp_types() const;

        float DispersionAngle;            // +0xa0  degrees * 0.01745329
        float TurnRate;                   // +0xa4  ctor 15 deg in rad
        float AzimuthMin;                 // +0xa8
        float AzimuthMax;                 // +0xac
        float ElevationMin;               // +0xb0
        float ElevationMax;               // +0xb4
        bool bAzimuthValid;               // +0xb8
        bool bElevationValid;             // +0xb9
        bool bAutoTurret;                 // +0xba
        unsigned char PadBb;              // +0xbb
        // Retail's ctor copies the low byte of its ICliObj* companion parameter here --
        // uninitialized-stack-reuse decompiler noise, not real data (same pattern as
        // ShieldGenerator::PadA8). Always zero in the port.
        unsigned char PadBc[4];                 // +0xbc..+0xbf
        // +0xc0/+0xc4/+0xc8: malloc'd [begin,end,cap) array of hp_gun_type ids (retail
        // VC6 vector<unsigned>, doubling growth). ~Gun frees HpGunTypesBegin.
        unsigned* HpGunTypesBegin;              // +0xc0
        unsigned* HpGunTypesEnd;                // +0xc4
        unsigned* HpGunTypesCap;                // +0xc8
        mutable bool bRangeCached;              // +0xcc  get_munition_range() memoized-flag
        unsigned char PadCd[3];
        mutable float CachedMunitionRange;      // +0xd0  get_munition_range() cached result

        HpAttachmentType get_hp_type_by_index(int Index) const;
    };

    static_assert(sizeof(Gun) == 0xd4, "Gun sizeof 0xd4");
    static_assert(offsetof(Gun, DispersionAngle) == 0xa0, "Gun+0xa0 dispersion_angle");
    static_assert(offsetof(Gun, TurnRate) == 0xa4, "Gun+0xa4 turn_rate");
    static_assert(offsetof(Gun, bAutoTurret) == 0xba, "Gun+0xba auto_turret");
    static_assert(offsetof(Gun, HpGunTypesBegin) == 0xc0, "Gun+0xc0 hp_gun_type array begin");
    static_assert(offsetof(Gun, HpGunTypesEnd) == 0xc4, "Gun+0xc4 hp_gun_type array end");
    static_assert(offsetof(Gun, HpGunTypesCap) == 0xc8, "Gun+0xc8 hp_gun_type array cap");
    static_assert(offsetof(Gun, bRangeCached) == 0xcc, "Gun+0xcc range cached flag");
    static_assert(offsetof(Gun, CachedMunitionRange) == 0xd0, "Gun+0xd0 cached munition range");

    struct COMMON_API MineDropper : public Launcher
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        MineDropper& operator=(MineDropper const&) = default;

        explicit MineDropper(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;
    };

    static_assert(sizeof(MineDropper) == 0xa0, "MineDropper sizeof 0xa0");

    struct COMMON_API CounterMeasureDropper : public Launcher
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        CounterMeasureDropper& operator=(CounterMeasureDropper const&) = default;

        explicit CounterMeasureDropper(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float AiRangeSq;   // +0xa0  ai_range, squared on read; default 0
    };

    static_assert(sizeof(CounterMeasureDropper) == 0xa4, "CounterMeasureDropper sizeof 0xa4");
    static_assert(offsetof(CounterMeasureDropper, AiRangeSq) == 0xa0,
                  "CounterMeasureDropper+0xa0 ai_range^2");

    struct COMMON_API Scanner : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Scanner& operator=(Scanner const&) = default;

        explicit Scanner(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float PowerThreshold;         // +0x70  power_usage, cargo scan power check
        float RadarRange;             // +0x74  range, GetRadarRange scan range multiplier
        float CargoScanRange;         // +0x78  cargo_scan_range, GetCargoScanRange
    };

    static_assert(sizeof(Scanner) == 0x7c, "Scanner sizeof 0x7c");

    struct COMMON_API Light : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Light& operator=(Light const&) = default;

        explicit Light(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual AClassType get_class_type() const;

        bool bAlwaysOn;       // +0x70  always_on, default true
        bool bDockingLight;   // +0x71  docking_light, default false
        unsigned char Pad72[2];
    };

    static_assert(sizeof(Light) == 0x74, "Light sizeof 0x74");
    static_assert(offsetof(Light, bAlwaysOn) == 0x70, "Light+0x70 always_on");
    static_assert(offsetof(Light, bDockingLight) == 0x71, "Light+0x71 docking_light");

    struct COMMON_API Tractor : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Tractor& operator=(Tractor const&) = default;

        explicit Tractor(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float MaxLength;   // +0x70  max_length, default 0
        float ReachSpeed;  // +0x74  reach_speed, default 100.0
    };

    static_assert(sizeof(Tractor) == 0x78, "Tractor sizeof 0x78");
    static_assert(offsetof(Tractor, MaxLength) == 0x70, "Tractor+0x70 max_length");
    static_assert(offsetof(Tractor, ReachSpeed) == 0x74, "Tractor+0x74 reach_speed");

    struct COMMON_API CargoPod : public AttachedEquipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        CargoPod& operator=(CargoPod const&) = default;

        explicit CargoPod(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;
    };

    static_assert(sizeof(CargoPod) == 0x88, "CargoPod sizeof 0x88");

    struct COMMON_API AttachedFXEquip : public Equipment
    {
    public:
        explicit AttachedFXEquip(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;

        // Retail Archetype::AttachedFXEquip::operator= (Common.dll.c 127878/127887/127893/
        // 127900/127964/127969/127974/127990), part of the "inherit=" shape-copy family --
        // reused verbatim for LootCrate too (same bare-Equipment shape); see
        // Equipment::InheritFrom.
        AttachedFXEquip& operator=(AttachedFXEquip const& Other);
    };

    static_assert(sizeof(AttachedFXEquip) == 0x70, "AttachedFXEquip sizeof 0x70");

    struct COMMON_API InternalFXEquip : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        InternalFXEquip& operator=(InternalFXEquip const&) = default;

        explicit InternalFXEquip(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;

        CacheString UseAnimation;   // +0x70  use_animation
    };

    static_assert(sizeof(InternalFXEquip) == 0x74, "InternalFXEquip sizeof 0x74");
    static_assert(offsetof(InternalFXEquip, UseAnimation) == 0x70, "InternalFXEquip+0x70 use_animation");

    struct COMMON_API RepairDroid : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        RepairDroid& operator=(RepairDroid const&) = default;

        explicit RepairDroid(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float RepairRate;   // +0x70  repair_rate, default 0
    };

    static_assert(sizeof(RepairDroid) == 0x74, "RepairDroid sizeof 0x74");
    static_assert(offsetof(RepairDroid, RepairRate) == 0x70, "RepairDroid+0x70 repair_rate");

    struct COMMON_API CloakingDevice : public AttachedEquipment
    {
    public:
        explicit CloakingDevice(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;

        // Retail Archetype::CloakingDevice::operator= (Common.dll.c 127987), part of the
        // "inherit=" shape-copy family; see Equipment::InheritFrom.
        CloakingDevice& operator=(CloakingDevice const& Other);

        float PowerUsage;      // +0x88  power_usage
        float CloakInTime;     // +0x8c  cloakin_time
        float CloakOutTime;    // +0x90  cloakout_time
        unsigned CloakInFx;    // +0x94  cloakin_fx, CreateID
        unsigned CloakOutFx;   // +0x98  cloakout_fx, CreateID
    };

    static_assert(sizeof(CloakingDevice) == 0x9c, "CloakingDevice sizeof 0x9c");
    static_assert(offsetof(CloakingDevice, PowerUsage) == 0x88, "CloakingDevice+0x88 power_usage");
    static_assert(offsetof(CloakingDevice, CloakOutFx) == 0x98, "CloakingDevice+0x98 cloakout_fx");

    // sizeof 0x7c. listing read 0x94550. get_class_type 0xa.
    struct COMMON_API Commodity : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Commodity& operator=(Commodity const&) = default;

        explicit Commodity(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        float get_decay_per_second() const;
        // Slot 7, 0x94720: LootAppearance (+0x74), or this when there is none.
        virtual Equipment* get_loot_appearance();
        // Slot 8, 0x94730: PodAppearance (+0x78) as stored.
        virtual CargoPod* get_pod_appearance();

        float DecayPerSecond;             // +0x70
        Equipment* LootAppearance;        // +0x74  GetEquipmentByName; miss → 0
        CargoPod* PodAppearance;          // +0x78  only if class 9
    };

    static_assert(sizeof(Commodity) == 0x7c, "Commodity sizeof 0x7c");
    static_assert(offsetof(Commodity, DecayPerSecond) == 0x70, "Commodity+0x70 decay_per_second");
    static_assert(offsetof(Commodity, LootAppearance) == 0x74, "Commodity+0x74 loot_appearance");
    static_assert(offsetof(Commodity, PodAppearance) == 0x78, "Commodity+0x78 pod_appearance");

    // sizeof 0x74. listing read 0x94d60 shared with ShieldBattery.
    struct COMMON_API RepairKit : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        RepairKit& operator=(RepairKit const&) = default;

        explicit RepairKit(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;
        // Slot 7, 0x94e30 (one body for RepairKit and ShieldBattery): LootAppearance (+0x70),
        // or this when there is none.
        virtual Equipment* get_loot_appearance();

        Equipment* LootAppearance;        // +0x70
    };

    static_assert(sizeof(RepairKit) == 0x74, "RepairKit sizeof 0x74");
    static_assert(offsetof(RepairKit, LootAppearance) == 0x70, "RepairKit+0x70 loot_appearance");

    struct COMMON_API ShieldBattery : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        ShieldBattery& operator=(ShieldBattery const&) = default;

        explicit ShieldBattery(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;
        // Slot 7, 0x94e30 (one body for RepairKit and ShieldBattery): LootAppearance (+0x70),
        // or this when there is none.
        virtual Equipment* get_loot_appearance();

        Equipment* LootAppearance;        // +0x70
    };

    static_assert(sizeof(ShieldBattery) == 0x74, "ShieldBattery sizeof 0x74");
    static_assert(offsetof(ShieldBattery, LootAppearance) == 0x70,
                  "ShieldBattery+0x70 loot_appearance");

    // sizeof 0x70 same as Equipment; get_class_type 0x1d.
    struct COMMON_API TradeLaneEquip : public Equipment
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        TradeLaneEquip& operator=(TradeLaneEquip const&) = default;

        explicit TradeLaneEquip(ICliObj* Companion = nullptr);
        virtual AClassType get_class_type() const;
    };

    static_assert(sizeof(TradeLaneEquip) == 0x70, "TradeLaneEquip sizeof 0x70");

    // sizeof 0x80. Base of Munition / Mine / CounterMeasure. listing read 0x96de0.
    struct COMMON_API Projectile : public Equipment
    {
    public:
        explicit Projectile(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        // Slot 7, 0x96f90: LootAppearance (+0x7c), or this when there is none.
        virtual Equipment* get_loot_appearance();

        // Retail Archetype::Projectile::operator= (Common.dll.c 22914-22930). Not part of
        // FUN_062f3ea0's dispatch itself (Munition/Mine/CounterMeasure::operator= each inline
        // this instead of calling it), but a real retail symbol; provided for completeness.
        Projectile& operator=(Projectile const& Other);

        float Lifetime;                   // +0x70
        float OwnerSafeTime;              // +0x74  default -1
        bool bRequiresAmmo;               // +0x78
        bool bForceGunOri;                // +0x79
        unsigned char Pad7a[2];
        Equipment* LootAppearance;        // +0x7c
    };

    static_assert(sizeof(Projectile) == 0x80, "Projectile sizeof 0x80");
    static_assert(offsetof(Projectile, Lifetime) == 0x70, "Projectile+0x70 lifetime");
    static_assert(offsetof(Projectile, bRequiresAmmo) == 0x78, "Projectile+0x78 requires_ammo");
    static_assert(offsetof(Projectile, LootAppearance) == 0x7c, "Projectile+0x7c loot_appearance");

    // sizeof 0xb0. listing read 0x97090. load 0x97450 (empty DA_archetype → BEAM_ARCH).
    struct COMMON_API Munition : public Projectile
    {
    public:
        explicit Munition(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual bool load();
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        // Retail `Archetype::Munition::BEAM_ARCH_INDEX`, export ordinal 1223, .data 0x063ecd20,
        // statically initialised to -1 (0xffffffff). Unlike the other recovered statics in this
        // wave it is NOT const -- it is the lazily-filled cache of the "BEAM_ARCH" compound
        // archetype handle, read at 062f7461/76b0/7700/930e/ab7f and written at
        // 062f76e9/7719/9347/ab98. This port already had the right value and lifecycle; only
        // the name was this port's own invention (BeamArchIndex), renamed here to the retail
        // exported name. Protected, as the mangling says (?BEAM_ARCH_INDEX@Munition@Archetype@@1JA);
        // declared in the protected section at the end of the class.
        static void LoadBeamArchIdx();
        static void FreeBeamArchIdx();

        bool is_guided() const { return Seeker != 0; }

        // Retail Archetype::Munition::operator= (Common.dll.c 127993), part of the
        // "inherit=" shape-copy family; see Equipment::InheritFrom.
        Munition& operator=(Munition const& Other);

        float HullDamage;                 // +0x80  damage / hull_damage
        float EnergyDamage;               // +0x84
        ID_String WeaponType;             // +0x88
        ID_String Motor;                  // +0x8c
        unsigned Seeker;                  // +0x90  0 none, 1 dumb, 2 lock
        float TimeToLock;                 // +0x94
        float SeekerRange;                // +0x98
        float SeekerFovCos;               // +0x9c  cos(deg * 0.01745329), default 1
        float MaxAngularVelocity;         // +0xa0  default 100
        float DetonationDistSq;           // +0xa4  dist * dist
        unsigned HpType;                  // +0xa8  PTR_s_hp_gun table, default 4
        bool bCruiseDisruptor;            // +0xac
        unsigned char PadAd[3];

    protected:
        // Protected in retail: ?free_resources@Munition@Archetype@@MAEXXZ (MAE) and
        // ?BEAM_ARCH_INDEX@Munition@Archetype@@1JA (1 = protected static).
        virtual void free_resources();
        static long BEAM_ARCH_INDEX;

        // ShieldGenerator::get_hp_type_by_index (062f5e40) reads its default table
        // DAT_063ecc98[0..0x22]; entry 0x22 is one past the 34-dword table and lands on
        // BEAM_ARCH_INDEX (.data 063ecd20), which retail returns as the hp type.
        friend struct ShieldGenerator;
    };

    static_assert(sizeof(Munition) == 0xb0, "Munition sizeof 0xb0");
    static_assert(offsetof(Munition, HullDamage) == 0x80, "Munition+0x80 hull_damage");
    static_assert(offsetof(Munition, Seeker) == 0x90, "Munition+0x90 seeker");
    static_assert(offsetof(Munition, HpType) == 0xa8, "Munition+0xa8 hp_type");

    struct COMMON_API Mine : public Projectile
    {
    public:
        explicit Mine(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        // Retail Archetype::Mine::operator= (Common.dll.c 127996), part of the "inherit="
        // shape-copy family; see Equipment::InheritFrom.
        Mine& operator=(Mine const& Other);

        float LinearDrag;                 // +0x80
        float DetonationDist;             // +0x84  not squared
        float SeekDist;                   // +0x88
        float Acceleration;               // +0x8c  default -1
        float TopSpeed;                   // +0x90  default 40
    };

    static_assert(sizeof(Mine) == 0x94, "Mine sizeof 0x94");
    static_assert(offsetof(Mine, LinearDrag) == 0x80, "Mine+0x80 linear_drag");
    static_assert(offsetof(Mine, TopSpeed) == 0x90, "Mine+0x90 top_speed");

    struct COMMON_API CounterMeasure : public Projectile
    {
    public:
        explicit CounterMeasure(ICliObj* Companion = nullptr);
        virtual bool read(INI_Reader& Reader);
        virtual void redefine(Root const& Other);
        virtual AClassType get_class_type() const;
        virtual HpAttachmentType get_hp_type() const;

        // Retail Archetype::CounterMeasure::operator= (Common.dll.c 127999), part of the
        // "inherit=" shape-copy family; see Equipment::InheritFrom.
        CounterMeasure& operator=(CounterMeasure const& Other);

        float LinearDrag;                 // +0x80
        float Range;                      // +0x84
        int DiversionPctg;                // +0x88  _ftol of diversion_pctg
    };

    static_assert(sizeof(CounterMeasure) == 0x8c, "CounterMeasure sizeof 0x8c");
    static_assert(offsetof(CounterMeasure, LinearDrag) == 0x80, "CounterMeasure+0x80 linear_drag");
    static_assert(offsetof(CounterMeasure, DiversionPctg) == 0x88,
                  "CounterMeasure+0x88 diversion_pctg");

    // Root + 3 floats. listing LoadAsteroids operator_new(0x68). Not Equipment.
    struct COMMON_API Asteroid : public Root
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        Asteroid& operator=(Asteroid const&) = default;

        explicit Asteroid(ICliObj* Companion = nullptr);

        virtual bool read(INI_Reader& Reader);
        virtual AClassType get_class_type() const;


        float get_detect_radius() const { return DetectRadius; }
        float get_recharge_time() const { return RechargeTime; }
        float get_offset_dist() const { return ExplosionOffset; }

        float DetectRadius;       // +0x5C default 25
        float ExplosionOffset;    // +0x60 default 0
        float RechargeTime;       // +0x64 default 1000
    };

    static_assert(sizeof(Asteroid) == 0x68, "Asteroid sizeof 0x68");
    static_assert(offsetof(Asteroid, DetectRadius) == 0x5C, "Asteroid+0x5C detect_radius");
    static_assert(offsetof(Asteroid, ExplosionOffset) == 0x60, "Asteroid+0x60 explosion_offset");
    static_assert(offsetof(Asteroid, RechargeTime) == 0x64, "Asteroid+0x64 recharge_time");

    // Root only. ctor 0x9a090 sets ObjectType 0x40000000. sizeof 0x5C.
    struct COMMON_API DynamicAsteroid : public Root
    {
    public:
        // User-declared so that no implicit move assignment is generated and exported (VC6
        // had none); retail's is the memberwise one, exactly what this defaults to.
        DynamicAsteroid& operator=(DynamicAsteroid const&) = default;

        explicit DynamicAsteroid(ICliObj* Companion = nullptr);
        // 0x9a0b0: `return Root::read(Reader) != false`.
        virtual bool read(INI_Reader& Reader);
        virtual AClassType get_class_type() const;
    };

    static_assert(sizeof(DynamicAsteroid) == 0x5C, "DynamicAsteroid sizeof 0x5C");

    // Not Root. LoadEquipment [Motor] operator_new(0x14). Vtable slot 0 = read, not dtor.
    struct MotorData
    {
    public:
        MotorData();
        // Retail vtable 063a1ecc: slot 0 read (062f7b00), slot 1 redefine (062f7ba0).
        bool read(INI_Reader& Reader);
        void redefine(MotorData const& Other);

        unsigned VtableUnused;    // +0x00
        ID_String Nickname;       // +0x04
        float Delay;              // +0x08 default 0
        float Accel;              // +0x0C default 15
        float Lifetime;           // +0x10 default 1.5
    };

    // effect = CreateID + optional strength (default -1). Retail VC6 list at Explosion+0x20.
    struct FExplosionEffect
    {
        ID_String Fx;
        float Strength;
    };

    // debris_type = CreateID + optional int stored as float. Retail vector at +0x50, stride 8.
    struct FExplosionDebrisType
    {
        ID_String Debris;
        float Count;
    };

    // Not Root. Retail sizeof 0x60 inline in map node DAT_063fcf40. Port: heap + MSVC vectors.
    // ctor 0xab730 / read 0xab7c0 / reset 0xabb30. process table DAT_063ed218.
    struct COMMON_API Explosion
    {
    public:
        // Common.dll.txt 0x10b730 (??0Explosion@Archetype@@QAE@ABUID_String@@@Z): copies the
        // nickname, then the same defaults reset() stores, with empty containers.
        explicit Explosion(ID_String const& InNickname);
        // User-declared so that no implicit move members are generated and exported: VC6 had
        // none, and retail exports exactly the memberwise copy ctor, dtor and this.
        Explosion& operator=(Explosion const&) = default;
        void reset();
        bool read(INI_Reader& Reader);

        ID_String Nickname;                          // +0x00
        float Radius;                                // +0x04
        float Impulse;                               // +0x08
        float HullDamage;                            // +0x0C
        float EnergyDamage;                          // +0x10
        unsigned Process;                            // +0x14 default 1 = disappear
        float Lifetime;                              // +0x18
        float LifetimeMax;                           // +0x1C default 10000
        std::vector<FExplosionEffect> Effects;       // retail list +0x20
        int NumChildPieces;                          // +0x2C
        float DebrisImpulse;                         // +0x30 default 5
        float InnardsDebrisStartTime;                // +0x34 default -1
        float InnardsDebrisNum;                      // +0x38
        float InnardsDebrisRadius;                   // +0x3C
        std::vector<unsigned> InnardsDebrisObjects;  // retail vector +0x40 of CreateID
        std::vector<FExplosionDebrisType> DebrisTypes; // retail vector +0x50
    };

    static_assert(sizeof(MotorData) == 0x14, "MotorData sizeof 0x14");
    static_assert(offsetof(MotorData, Nickname) == 0x04, "Motor+0x04 nickname");
    static_assert(offsetof(MotorData, Delay) == 0x08, "Motor+0x08 delay");
    static_assert(offsetof(MotorData, Accel) == 0x0C, "Motor+0x0C accel");
    static_assert(offsetof(MotorData, Lifetime) == 0x10, "Motor+0x10 lifetime");

    // DAT_063fcafc vector + DAT_063fcb0c map. Listing 0x994e0 / 0x99530.
    // ?LargeIDToSmallID@Archetype@@YAGI@Z
    // ?SmallIDToLargeID@Archetype@@YAIG@Z
    COMMON_API unsigned short __cdecl LargeIDToSmallID(unsigned LargeId);
    COMMON_API unsigned __cdecl SmallIDToLargeID(unsigned short SmallId);

    // DecodeType (the OBJECT_TYPE name -> bitmask lookup) is a GLOBAL export in retail
    // (?DecodeType@@YAIPBD@Z) -- declared after this namespace, at the end of this header.

    // ------------------------------------------------------------------------
    // decomp-tracker items 1938/1939/1940/1941/1942/1949/1951/1954/1974/1979/1984
    // (Common.dll.c/.txt 0x062fabb0-0x062fd410, d222:cluster-062eb550 pass,
    // 2026-09-10) -- raw VC6 `std::_Tree<Traits>` (map) INTERNALS for exactly
    // the "separate std::map instances (DAT_063fca84 ... DAT_063fcaf8)" this
    // class's own header comment above already names. Investigated individually
    // (not bulk-classified) and confirmed, not guessed:
    //   * FUN_062fb070/FUN_062fb0e0 (1941/1942): the generic 0x44-byte-node
    //     _Tree_nod constructor/destructor (allocate/free the Nil+Head sentinel
    //     pair, self-linking an empty tree) -- byte-for-byte the classic
    //     Dinkumware `_Tree_nod()`/`~_Tree_nod()` shape, confirmed against the
    //     raw disasm (Common.dll.txt:275886-276031), not the (wrongly single-
    //     byte-typed) Ghidra pseudocode.
    //   * FUN_062fabb0/FUN_062facf0/FUN_062fae00 (1938/1939/1940): per-global
    //     teardown routines operating directly on the DAT_063fca74/84/9c/ec-
    //     series statics -- each walks its own tree, and two of the three
    //     (1938/1940) additionally warn on dangling references via FDUMP with
    //     the format strings "Archetype::SimpleDB %s has %d dangling
    //     references.\n" / "Archetype::Asteroid %s ..." / "Archetype::
    //     DynamicAsteroid %s ..." (Common.dll.c:133736/133864/133891) before
    //     releasing each entry (vtable slot 0, arg 1) and erasing it.
    //   * FUN_062fb890/FUN_062fbad0/FUN_062fcf30/FUN_062fd060 (1949/1954/1974/
    //     1979): in-order "get Nth element" walk, `lower_bound`-style binary
    //     search, recursive subtree copy, and a textbook CLRS red-black
    //     insert-with-fixup (FUN_062fd060 alone is ~200 decompiled lines,
    //     matching the algorithm shape exactly) -- all operating on the SAME
    //     0x44-byte node shape (Left/Parent/Right at +0/+4/+8, Value at +0xc,
    //     Color/IsNil bytes at +0x40/+0x41).
    //   * FUN_062fd3e0 (1984): the mapped-value's own "destroy, then maybe
    //     free" wrapper, called only from 1942's per-node cleanup loop
    //     (Common.dll.txt:275976-275979, `LEA ECX,[EBX+0xc]; CALL FUN_062fd3e0`).
    //
    // ArchDB below (already ported, pre-existing this pass) is the project's
    // OWN already-shipped, functionally-equivalent replacement for this exact
    // machinery -- confirmed, not assumed: ArchDB::FImpl stores a REAL
    // `std::map<unsigned, void*> Records` (common_archetype.cpp ArchDB::FImpl),
    // ArchDB::Insert's own doc comment cites "FUN_062fbeb0 insert" (the
    // insert entry point that itself calls FUN_062fbad0 lower_bound +
    // FUN_062fd060 rebalance-insert, items 1954/1979 above), and
    // item 1949's own "get Nth element by index" walk (FUN_062fb890) is
    // ArchDB::KeyAt, which Archetype::GetId uses for every class (retail
    // inlines the same walk for five classes and calls 062fb890 for the
    // sixth). This is the SAME
    // "replace identified raw _Tree machinery with a real std::map once the
    // owning container is known" convention already established elsewhere in
    // this port (CArchGroupManager::Traverse, common_carchgroup.cpp).
    //
    // Per this project's "skipped vs ported-name for rewrites" convention, all
    // eleven items above are marked `skipped` with ported_name pointing at
    // Archetype::ArchDB (this header) rather than `ready_unverified` -- they
    // are a full functional rewrite, not a 1:1 port, and 1:1-porting the raw
    // VC6 tree internals here on top of the already-complete ArchDB would only
    // reintroduce the exact duplication ArchDB was written to replace.
    //
    // The per-map teardown (062fabb0 / 062facf0 / 062fae00 and the inline walks in
    // 062fa980) -- release loop, dangling-reference warning, scalar deleting dtor --
    // lives in Archetype::Free's per-record callbacks (common_archetypeloader.cpp); ArchDB::Clear
    // only empties the map.
    //
    // decomp-tracker item 1944 (FUN_062fb1e0, Common.dll.c:134030-134055) is a
    // SEPARATE raw-array copy-construct helper for a 0x14-byte-element vector
    // (matching FUN_062fb260's sibling destructor, Ghidra-typed `Loadout::
    // Map::~Map`, not in this pass's item range) -- superseded the same way by
    // this project's already-existing `Loadout::Map` (common_goods.h), which
    // stores a real `std::vector<EquipDesc> Items` rather than retail's raw
    // fixed-layout array. Also marked `skipped`, ported_name Loadout::Map.
    // ------------------------------------------------------------------------

    // ArchDB's out-of-line pieces (common_archetype.cpp): the three dumps and the small-id
    // registry shared by every map.
    void ArchDBDumpNoNickname(char const* KindName);      // line 0x84
    void ArchDBDumpMiss(unsigned NicknameId);             // line 0x3c
    void ArchDBDumpMiss(ID_String const& Name);           // line 0x49
    void RegisterLargeId(unsigned LargeId);

    // One typed map per record kind. Retail keeps separate std::map instances (DAT_063fca84 …
    // DAT_063fcaf8), each holding pointers of a single archetype class, so the value type is
    // the record class itself.
    template <typename T>
    class ArchDB
    {
    public:
        // bIndexIds: every insert that registers small ids into DAT_063fcafc / DAT_063fcb0c --
        // FUN_062fbeb0 (Ship, Solar, Asteroid, DynamicAsteroid), FUN_062fbbe0 (Equipment), the
        // inline Simple (FUN_062f0dc0) and Motor (LoadEquipment 062f7da2) inserts.
        explicit ArchDB(bool bIndexIdsIn)
            : bIndexIds(bIndexIdsIn)
        {
        }

        ArchDB(ArchDB const&) = delete;
        ArchDB& operator=(ArchDB const&) = delete;

        // FUN_062fbeb0 / FUN_062fbbe0 (thiscall, RET 0xC: Record, &Nickname, KindName,
        // 062fbbe0..062fbcbe). Nickname.Id == 0 -> FDUMP(0x100001, "... Loaded '%s' has no
        // nickname ...", line 0x84, KindName), false. Otherwise map insert (FUN_063222f0); only
        // a new key registers its small id (when bIndexIds) and returns true; a duplicate
        // returns false without any dump -- the duplicate handling (warning, redefine, delete)
        // belongs to each loader. Does not take ownership.
        bool Insert(T* Record, ID_String const& Nickname, char const* KindName)
        {
            unsigned const NicknameId = Nickname.Id;
            if (NicknameId == 0)
            {
                ArchDBDumpNoNickname(KindName);
                return false;
            }
            if (!Records.insert(std::pair<unsigned const, T*>(NicknameId, Record)).second)
                return false;
            if (bIndexIds)
                RegisterLargeId(NicknameId);
            return true;
        }

        // Silent map::find (FUN_06301640). Miss → nullptr, no FDUMP.
        T* Find(unsigned NicknameId) const
        {
            typename std::map<unsigned, T*>::const_iterator Found = Records.find(NicknameId);
            if (Found == Records.end())
                return nullptr;
            return Found->second;
        }
        T* Find(char const* Nickname) const { return Find(CreateID(Nickname)); }

        // ArchDB::Get — FDUMP ERROR on miss (lines 0x3c / 0x49), then nullptr.
        T* Get(unsigned NicknameId) const
        {
            T* Record = Find(NicknameId);
            if (Record == nullptr)
                ArchDBDumpMiss(NicknameId);
            return Record;
        }
        T* Get(ID_String const& Name) const
        {
            T* Record = Find(Name.Id);
            if (Record == nullptr)
                ArchDBDumpMiss(Name);
            return Record;
        }

        // Empties the map. Does not Release / delete records (Archetype::Free does that).
        void Clear() { Records.clear(); }

        unsigned Size() const { return (unsigned)Records.size(); }

        // FUN_062fb890 (thiscall, RET 4): 062fb89a CMP Index,[this+0x10] / JNC -> return 0;
        // otherwise 062fb8a3..062fb8f8 walk from begin() in key order, counting, and 062fb902
        // returns the key [node+0xc] of the Index-th node; reaching the head returns 0 (062fb8fa).
        unsigned KeyAt(unsigned Index) const
        {
            if (Index >= Size())
                return 0;
            unsigned Count = 0;
            typename std::map<unsigned, T*>::const_iterator It = Records.begin();
            for (; It != Records.end(); ++It)
            {
                if (Count == Index)
                    return It->first;
                Count += 1;
            }
            return 0;
        }

        // Walks values. Does not Clear. Fn may delete the record; do not Insert/erase during visit.
        void ForEach(void (*Fn)(T* Record)) const
        {
            if (Fn == nullptr)
                return;
            typename std::map<unsigned, T*>::const_iterator It = Records.begin();
            for (; It != Records.end(); ++It)
                Fn(It->second);
        }

        // The Enum* walk (EnumShips 062f3310 and its five siblings): in key order from begin(),
        // Enumerator slot 0 on each record, stop at the first false. No null check on
        // Enumerator -- retail dereferences it as soon as the map is non-empty.
        void Enumerate(ArchEnumerator* Enumerator) const
        {
            typename std::map<unsigned, T*>::const_iterator It = Records.begin();
            for (; It != Records.end(); ++It)
            {
                if (!Enumerator->Enumerate(It->second))
                    return;
            }
        }

    private:
        std::map<unsigned, T*> Records;
        bool bIndexIds;
    };

    extern ArchDB<Root> GSimpleDb;
    extern ArchDB<Ship> GShipDb;
    extern ArchDB<Solar> GSolarDb;
    extern ArchDB<Equipment> GEquipmentDb;
    extern ArchDB<Asteroid> GAsteroidDb;
    extern ArchDB<DynamicAsteroid> GDynamicAsteroidDb;
    extern ArchDB<MotorData> GMotorDb;

    // Empties every typed map. Does not delete records.
    void ClearAllArchDBs();
    // Retail DAT_063fcf3c: a map that holds each Explosion by value (node value at +0x10,
    // GetExplosion 0630bd10 returns &node->value). Not an ArchDB: LoadExplosion inserts
    // without the nickname / duplicate dumps, and FreeExplosions only clears it.
    extern std::map<unsigned, Explosion> GExplosionDb;

    // Typed Get wrappers — FDUMP on miss (ArchDB::Get behavior).
    COMMON_API Ship* __cdecl GetShip(unsigned NicknameId);
    COMMON_API Ship* __cdecl GetShipByName(ID_String const& Name);
    COMMON_API Solar* __cdecl GetSolar(unsigned NicknameId);
    COMMON_API Solar* __cdecl GetSolarByName(ID_String const& Name);
    COMMON_API Equipment* __cdecl GetEquipment(unsigned NicknameId);
    COMMON_API Equipment* __cdecl GetEquipmentByName(ID_String const& Name);
    COMMON_API Root* __cdecl GetSimple(unsigned NicknameId);
    COMMON_API Root* __cdecl GetSimpleByName(ID_String const& Name);
    COMMON_API Asteroid* __cdecl GetAsteroid(unsigned NicknameId);
    COMMON_API Asteroid* __cdecl GetAsteroidByName(ID_String const& Name);
    COMMON_API DynamicAsteroid* __cdecl GetDynamicAsteroid(unsigned NicknameId);
    COMMON_API DynamicAsteroid* __cdecl GetDynamicAsteroidByName(ID_String const& Name);
    COMMON_API MotorData const* __cdecl GetMotor(unsigned NicknameId);
    COMMON_API Explosion* __cdecl GetExplosion(ID_String const& Name);

    // Enum* callbacks — walk each DB and call enumerator until it returns false.
    COMMON_API void __cdecl EnumShips(ArchEnumerator* Enumerator);
    COMMON_API void __cdecl EnumSolars(ArchEnumerator* Enumerator);
    COMMON_API void __cdecl EnumEquipment(ArchEnumerator* Enumerator);
    COMMON_API void __cdecl EnumSimples(ArchEnumerator* Enumerator);
    COMMON_API void __cdecl EnumAsteroids(ArchEnumerator* Enumerator);
    COMMON_API void __cdecl EnumDynamicAsteroids(ArchEnumerator* Enumerator);

    // ID allocation helpers.
    COMMON_API unsigned __cdecl GetId(AClassType ClassType, unsigned Index);
    COMMON_API unsigned __cdecl GetIdCount(AClassType ClassType);

    // Max hit points for an archetype by large ID. Retail (0x99570) checks only
    // Ship, then Solar, then Equipment (in that order) via ArchDB::Get (FDUMP on
    // miss), truncates the found Root::HitPts float to int (ftol), and defaults
    // to 1 if all three lookups miss or find a null record.
    COMMON_API int __cdecl GetArchMaxHitPts(unsigned LargeId);

    // Mesh bind — Common.dll listing 0x8fd80 (IFileSystem*) / 0x8fe40 (path).
    // CRC hit → IEngine+0x44; miss → open FS + IEngine+0x30. PhyArch surfaces not here.
    COMMON_API bool __cdecl Load(char const* Path, long* EngineIndex, int* AnimIndex);
    COMMON_API bool __cdecl Load(IFileSystem* Filesys, long* EngineIndex, int* AnimIndex);

    // Table loaders (common_archetypeloader.cpp). int = ArchDB insert delta (0 = open fail /
    // nothing added).
    // bool = reload (true → redefine on duplicate nickname). DataConfig passes false.
    COMMON_API int __cdecl LoadEquipment(char const* Path, bool bReload, ICliObjFactory* Factory);
    COMMON_API int __cdecl LoadSolar(char const* Path, bool bReload, ICliObjFactory* Factory);
    COMMON_API int __cdecl LoadShips(char const* Path, bool bReload, ICliObjFactory* Factory);
    COMMON_API int __cdecl LoadAsteroids(char const* Path, ICliObjFactory* Factory);
    COMMON_API int __cdecl LoadSimples(char const* Path, bool bReload, ICliObjFactory* Factory);
    // No LoadExplosions export: DataConfig [Data] explosions = LoadSimples; fuses = FuseDB.
    // Internal: LoadEquipment / LoadSolar / LoadShips / LoadSimples on header [Explosion].
    COMMON_API bool __cdecl LoadExplosion(INI_Reader& Reader, bool bReload);
    COMMON_API void __cdecl Free();
    COMMON_API void __cdecl FreeExplosions();
}

// Common.dll.txt:062ef3d0, ?DecodeType@@YAIPBD@Z (ordinal 1348, global scope). Maps an
// OBJECT_TYPE name (e.g. "FIGHTER", "ASTEROID") to its bitmask via a fixed 29-entry name table
// -- NOT a CreateID/CRC hash. Unknown name: FDUMP warning, 0.
COMMON_API unsigned __cdecl DecodeType(char const* Name);

struct IEngine;

// (ArchetypeLoadFromPath moved to freelancer.exe -- src/freelancer/Client/client_mesh_load.h.
// It is a client-only port helper; retail Common.dll does not export it.)
