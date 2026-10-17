// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "object_prop.h"

#include "client_cliobj.h"
#include "engine_light.h"
#include "game_options.h"
#include "misc_util.h"

#include "DALib.h"
#include "common_archetype.h"
#include "common_fuses.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_newchar.h"
#include "common_universe.h"
#include "dacom.h"
#include "engine/engbase.h"

#include <stdint.h>
#include <string.h>

namespace
{
    // .rdata 0x005cbc74 -- the source path ObjectProp.cpp hands FDUMP.
    char const kObjectPropCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ObjectProp.cpp";

    // The HardpointSummary FUN_004467e0 allocates (operator new(0x20), then the Common
    // constructor, then vptr 0x005cbd68). That vtable's only slot is FUN_004468d0, the scalar
    // deleting destructor: ~HardpointSummary, then operator delete on flag bit 0.
    class FObjectPropHardpoints : public HardpointSummary
    {
    public:
        ~FObjectPropHardpoints() override {}
    };

    // FUN_005595a0 (cdecl) -- FUN_00406cd0(Handle, 0, 0) on the companion unless Handle is -1.
    void RecomputeCompanionLod(Archetype::ICliObj* Companion, long Handle)
    {
        if (Handle == -1)
            return;
        Archetype::CliObjRecomputeLodPickExplicit(Companion, Handle, nullptr, false);
    }

    // The 0x30-byte frame FUN_004464f0 builds at [ESP+0xc] for Engine +0x9c: the 9-dword
    // orientation first (0x00446780..0x00446789 MOVSD.REP), then the position (+0x24..+0x2c).
    struct FInstanceMatrix
    {
        Matrix Orient;
        Vector Pos;
    };
    static_assert(sizeof(FInstanceMatrix) == 0x30, "engine instance matrix 0x30");

    // FUN_00446ac0 / FUN_00446b40 0x00446ac3..0x00446b09: which options show a prop of Type.
    bool IsPropTypeShown(int Type)
    {
        if (Type == 0)
            return GetGameOption(kGameOptBackgroundGroup) >= 1;
        if (Type == 1)
            return GetGameOption(kGameOptObjectsGroup) != 0 && GetGameOption(kGameOptShips) != 0;
        return GetGameOption(kGameOptObjectsGroup) != 0;
    }
}

FPropData::FPropData()
    : Type(-1)
{
    // FUN_00445e20.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FObjectProp::FObjectProp(FRoomRuntime* InOwner, FPropData const& Data)
    : FRoomObject(InOwner)
    , Instance(kRoomObjectNoInstance)
    , Type(0)
    , bDetached(0)
    , bRoomMain(0)
    , Hardpoints(nullptr)
{
    // FUN_00445f40. 0x00445f7f..0x00445f89: FUN_00555a40 on +0x4c; 0x00445f9c..0x00445fb7: the
    // +0x68 list head and FUN_00558180 on +0x74 (the member constructors above).
    Scripts.Init();
    // 0x00445fc9..0x00445fe2: +0x30 = -1, +0x48 = +0x49 = 0, FUN_00555a70 on +0x4c, +0x64 = 0.
    Scripts.Reset();
    // 0x00445fe5.
    Load(Data);
}

FObjectProp::~FObjectProp()
{
    // FUN_00446020. 0x00446046..0x0044608a: unless +0x48, Engine +0x60 on every attached
    // instance, then on +0x30 -- when +0x30 is not -1.
    if (bDetached == 0 && Instance != kRoomObjectNoInstance)
    {
        std::list<void*>::iterator It = Attached.begin();
        for (; It != Attached.end(); ++It)
            DALib::Engine->DestroyInstance(*It);
        DALib::Engine->DestroyInstance(Instance);
    }
    // 0x0044608d: FUN_00555a90 on +0x4c.
    Scripts.Destroy();
    // 0x00446095..0x004460a0: the HardpointSummary through its vtable slot 0 with 1.
    if (Hardpoints != nullptr)
    {
        delete Hardpoints;
        Hardpoints = nullptr;
    }
    // 0x004460a4..0x0044613e: FUN_005581b0 on +0x74, the +0x68 list, the thunk FUN_00555a90 on
    // +0x4c (a second pass over the table the call above already emptied -- only stores), the
    // +0x38 string, then FUN_005594b0: the member and base destructors.
}

void FObjectProp::Update(double DeltaSeconds)
{
    // FUN_00446a60. 0x00446a63..0x00446a7a: FUN_00558470 on +0x74 with +0x64 while +0x14 is set.
    float const Delta = static_cast<float>(DeltaSeconds);
    if (Flag14 != 0)
        Flashlights.Update(Hardpoints, Delta);
    // 0x00446a7f..0x00446a9b: Anim +0x6c (Instance, Delta).
    DALib::Anim->MixerWalk(reinterpret_cast<unsigned long>(Instance), Delta);
    // 0x00446a9e..0x00446ab0: Engine +0x6c (Instance, 2, Delta).
    DALib::Engine->UpdateInstance(Instance, 2, AsOpaqueDword(Delta));
}

void* FObjectProp::GetEngineInstance()
{
    // LAB_00443870: MOV EAX,[ECX+0x30].
    return Instance;
}

void FObjectProp::DrawOpaque(ICamera* /*Camera*/)
{
    // FUN_00446ac0.
    if (!IsPropTypeShown(Type))
        return;
    // 0x00446b0b..0x00446b10.
    if (bDetached != 0)
        return;
    // 0x00446b12..0x00446b27: a type-5 prop first casts its shadow (FUN_0044b370 with the
    // slot-6 instance, 0, 0).
    if (Type == 5)
    {
        RoomControlDrawShadow(Owner, reinterpret_cast<unsigned long>(GetEngineInstance()), 0,
                              0);
    }
    // 0x00446b2c..0x00446b30: FUN_0044af60.
    RoomControlDrawObject(Owner, this);
}

void FObjectProp::DrawTranslucent(ICamera* Camera)
{
    // FUN_00446b40: the same option gate, then FUN_00558580 on +0x74 unless +0x48, when +0x14.
    if (!IsPropTypeShown(Type))
        return;
    if (bDetached != 0)
        return;
    if (Flag14 == 0)
        return;
    Flashlights.Draw(Camera);
}

void FObjectProp::Load(FPropData const& Data)
{
    // FUN_004464f0. 0x004464f0..0x00446516: +0x38 = the name, +0x34 = the type.
    Name = Data.Name;
    Type = Data.Type;
    char const* Template = Data.Template.c_str();

    Archetype::Root* Arch = nullptr;
    switch (Data.Type)
    {
    case 0:
    case 5:
    case 6:
    {
        // 0x00446720..0x0044673f: the PetalDB record of that category.
        unsigned const Id = ResolveTemplate(Template);
        PetalInterfaceDatabase::petal_record const* Record =
            PetalDB_get()->find_record(Data.Type, Id);
        LoadPetal(Record, Template);
        break;
    }
    case 1:
        Arch = Archetype::GetShip(ResolveTemplate(Template));
        break;
    case 2:
        Arch = Archetype::GetSolar(ResolveTemplate(Template));
        break;
    case 3:
        Arch = Archetype::GetAsteroid(ResolveTemplate(Template));
        break;
    case 4:
        Arch = Archetype::GetEquipment(ResolveTemplate(Template));
        break;
    default:
        break;
    }

    if (Data.Type >= 1 && Data.Type <= 4)
    {
        if (Arch == nullptr)
        {
            // 0x004465b4 / 0x004465f6 / 0x004466f3: the equipment case builds the severity with
            // FUN_00411c50(0x10000, 1), the others pass 0x100001 directly -- the same value.
            unsigned Severity = 0x100001;
            if (Data.Type == 4)
                PackNibbleByte(&Severity, 0x10000, 1);
            FDUMP(Severity, "%s(%d) : *** ERROR: No valid archetype for prop %s\n",
                  kObjectPropCpp, 0x69, Template);
        }
        else
        {
            // 0x00446562..0x004465ac (0x004466a4..0x004466ec for solars and equipment):
            // +0x13 = the companion's envmap material is not empty, then FUN_0041c540 on the
            // archetype's mesh with the companion's material list, and FUN_005595a0.
            Archetype::ICliObj* Companion = Arch->Companion;
            bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
            long Handle = -1;
            Instance = MiscUtilLoadEngineInstance(Arch->DaArchetype.Value, DALib::Data, &Scripts,
                                                  nullptr, Archetype::CliObjMaterialPaths(Companion),
                                                  &MaterialEntries, &Handle);
            RecomputeCompanionLod(Companion, Handle);
        }
    }

    // 0x00446744..0x00446772: a ship with a loadout, while the "ship_loadouts" slider is on.
    if (Data.Type == 1 && Data.Loadout.length() != 0 && RtcSlider::is_enabled("ship_loadouts"))
        LoadLoadout(Data.Loadout.c_str());

    // 0x00446777..0x004467c9: the description's frame into the instance, then Engine +0x6c
    // (Instance, 0, 0).
    FInstanceMatrix Xform;
    Xform.Orient = Data.Orient;
    Xform.Pos = Data.Pos;
    DALib::Engine->SetInstanceMatrix(Instance, &Xform);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

void FObjectProp::LoadPetal(void const* RecordPtr, char const* Template)
{
    // FUN_00446160.
    PetalInterfaceDatabase::petal_record const* Record =
        static_cast<PetalInterfaceDatabase::petal_record const*>(RecordPtr);
    if (Record == nullptr)
    {
        // 0x00446240..0x00446260.
        FDUMP(0x100001, "%s(%d) : *** ERROR: No valid PetalDB archetype for prop %s\n",
              kObjectPropCpp, 0x86, Template);
        return;
    }

    // 0x0044618b..0x004461e4: the record's extra material names, each as {name, -1} through
    // FUN_0040d830 (CacheString::clear first, then the name stored over it).
    Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> Extras;
    Extras.Cookie = 0;
    Extras.Begin = nullptr;
    Extras.End = nullptr;
    Extras.Capacity = nullptr;
    std::vector<char const*>::const_iterator It = Record->Extras.begin();
    for (; It != Record->Extras.end(); ++It)
    {
        CacheString Path;
        Path.clear();
        Path.Value = *It;
        Archetype::PushMaterialPathPair(&Extras, Path.Value, 0xffffffffu);
    }

    // 0x004461ed..0x0044621c: FUN_0041c540 on the record's mesh (or "") into +0x30.
    char const* Mesh = Record->Mesh.c_str();
    Instance = MiscUtilLoadEngineInstance(Mesh, DALib::Data, &Scripts, nullptr, &Extras,
                                          &MaterialEntries, nullptr);
    // 0x0044621f..0x00446224: the list's buffer.
    delete[] Extras.Begin;
}

unsigned FObjectProp::ResolveTemplate(char const* Template)
{
    // FUN_00446280. 0x004464d9: an ordinary name is its CreateID.
    if (Template[0] != '$')
        return CreateID(Template);

    // 0x00446294..0x004464a7: the current base's slot for the indirection. Retail reads each
    // through BASE's vtable (+0x18 .. +0x50, getters of +0x30 .. +0x68, Common .rdata
    // 0x063a4d74..0x063a4da8); the port's Universe::IBase publishes the same fields directly.
    Universe::IBase* Base = Universe::get_base(GetRoomRuntimeBaseId(Owner));
    if (_stricmp(Template, "$ship_sml_01") == 0)
        return Base->ShipSml[0];
    if (_stricmp(Template, "$ship_sml_02") == 0)
        return Base->ShipSml[1];
    if (_stricmp(Template, "$ship_sml_03") == 0)
        return Base->ShipSml[2];
    if (_stricmp(Template, "$ship_mdm_01") == 0)
        return Base->ShipMdm[0];
    if (_stricmp(Template, "$ship_mdm_02") == 0)
        return Base->ShipMdm[1];
    if (_stricmp(Template, "$ship_mdm_03") == 0)
        return Base->ShipMdm[2];
    if (_stricmp(Template, "$ship_lrg_01") == 0)
        return Base->ShipLrg[0];
    if (_stricmp(Template, "$ship_lrg_02") == 0)
        return Base->ShipLrg[1];
    if (_stricmp(Template, "$ship_lrg_03") == 0)
        return Base->ShipLrg[2];
    if (_stricmp(Template, "$terrain_tiny") == 0)
        return Base->TerrainTiny;
    if (_stricmp(Template, "$terrain_sml") == 0)
        return Base->TerrainSml;
    if (_stricmp(Template, "$terrain_mdm") == 0)
        return Base->TerrainMdm;
    if (_stricmp(Template, "$terrain_lrg") == 0)
        return Base->TerrainLrg;
    if (_stricmp(Template, "$terrain_dyna_01") == 0)
        return Base->TerrainDyna01;
    if (_stricmp(Template, "$terrain_dyna_02") == 0)
        return Base->TerrainDyna02;

    // 0x004464b0..0x004464d6.
    FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid indirection template name %s in prop\n",
          kObjectPropCpp, 0xd8, Template);
    return 0;
}

void FObjectProp::LoadLoadout(char const* LoadoutName)
{
    // FUN_004467e0.
    Loadout::Map const* LoadoutMap = Loadout::Get(CreateID(LoadoutName));
    if (LoadoutMap == nullptr)
    {
        // 0x00446890..0x004468ae.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to find loadout %s\n", kObjectPropCpp, 0x158,
              LoadoutName);
        return;
    }

    // 0x00446815..0x00446854: +0x64 = a new summary, then analyze_instance(+0x30).
    Hardpoints = new FObjectPropHardpoints();
    Hardpoints->analyze_instance(reinterpret_cast<long>(Instance));

    // 0x0044685a..0x00446876: FUN_004468f0 on every item with +0x64.
    std::vector<EquipDesc>::const_iterator It = LoadoutMap->Items.begin();
    for (; It != LoadoutMap->Items.end(); ++It)
        AttachEquipment(&*It, Hardpoints);
}

void FObjectProp::AttachEquipment(void const* ItemPtr, HardpointSummary* Summary)
{
    // FUN_004468f0.
    EquipDesc const* Item = static_cast<EquipDesc const*>(ItemPtr);
    Archetype::Equipment* Arch = Archetype::GetEquipment(Item->get_arch_id());
    Archetype::ICliObj* Companion = Arch->Companion;

    // 0x00446909..0x0044694e: a light that is always on blinks on its hardpoint.
    if (Arch->get_class_type() == Archetype::AClassLight)
    {
        if (!static_cast<Archetype::Light*>(Arch)->bAlwaysOn)
            return;
        int const Index = Summary->find_name(Item->get_hardpoint().Value);
        if (Index == -1)
            return;
        Flashlights.Add(Companion, Index);
        return;
    }

    // 0x00446951..0x0044695a: anything else only when it is attached equipment.
    if (!Arch->is_attached())
        return;

    // 0x00446960..0x004469ba: FUN_0041c540 without a script table or handle, appended to +0x68.
    void* Child = MiscUtilLoadEngineInstance(Arch->DaArchetype.Value, DALib::Data, nullptr,
                                             nullptr, Archetype::CliObjMaterialPaths(Companion),
                                             &MaterialEntries, nullptr);
    Attached.push_back(Child);

    // 0x004469bd..0x004469e4: Engine +0x64 (the instance's archetype) into FUN_00406cd0, then
    // Engine +0x3c on it.
    void* ArchHandle = DALib::Engine->GetInstanceArchetype(Child);
    Archetype::CliObjRecomputeLodPickExplicit(Companion, reinterpret_cast<long>(ArchHandle),
                                              nullptr, false);
    DALib::Engine->ReleaseHandle(ArchHandle);

    // 0x004469e7..0x00446a47: with both hardpoint names, connect the item's HP_child to the
    // loadout hardpoint (Hardpoint +0x18) when the summary knows that hardpoint.
    char const* HpName = Item->get_hardpoint().Value;
    char const* HpChild = static_cast<Archetype::AttachedEquipment*>(Arch)->HpChild.Value;
    if (HpName == nullptr || HpChild == nullptr)
        return;
    long const Parent = Summary->find_name_instance(HpName);
    if (Parent == -1)
        return;
    FHardpointKey ChildKey;
    ChildKey.Name = HpChild;
    ChildKey.Crc = DACOM_CRC::GetCRC32(HpChild);
    FHardpointKey ParentKey;
    ParentKey.Name = HpName;
    ParentKey.Crc = DACOM_CRC::GetCRC32(HpName);
    DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(Parent), &ParentKey, Child,
                                        &ChildKey);
}
