// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "space_solar_world.h"

#include "character_select_equip.h"
#include "client_cliobj.h" // Archetype::LoadMountedEquipmentCompanions / HookUpInstanceTreeMaterials
#include "client_gameobj.h"
#include "client_objdb.h"

#include "common_archetype.h"
#include "common_cobject.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "render/rendcomp.h"

#include <cstdarg>
#include <cstdint>

#include <cstdio>
#include <cstring>
#include <new>
#include <windows.h>

// The anim-name list CSolar::ReadObj fills ("anim" keys) is CSolar::CreateParms::ContinualAnims,
// a std::list member: Parms' destructor frees it, as retail's inlined ~CreateParms does on both
// exits (0x00548c95..0x00548d15 and 0x00548d1f..0x00548d80: the +0xe4 list, then the +0xac list
// and the +0x9c EquipDescVector).
bool SpawnWorldSolarFromObjectSection(INI_Reader& Reader)
{
    // Retail FUN_00548a20:
    //   get_state → ReadObj → operator_new(0x208) FUN_005475b0/FUN_00532bc0(2)
    //   → create(+0x168)=FUN_005477d0 → CSolar::init(ObjDb=PTR_DAT_00613ed0)
    //   → FUN_005485c0 → FUN_00540c30(gameobj,0) → CliObjLoad(1) → Release arch
    // Visual handle for neighbor: Solar->EngineInstanceId (IObjInspect::get_index).
    // Vibe::Verify in ReadObj is seeded by SolarReps::LoadObjectVibesFromSystemIni
    // during Universe::LoadSystemFile — not by ad-hoc EnsureExists.
    // FUN_005485c0 is FClientSolarGameobj::ReadIni (client_gameobj.cpp).
    // Phase: LoadSpaceSceneSlice / connect — not inside BeginScene.

    // 0x00548a46..0x00548a50: the reader's position at the section start, restored before the
    // client half re-reads the section (0x00548c73).
    INI_Reader::State SectionStart;
    Reader.get_state(SectionStart);

    // 0x00548a56..0x00548b23: the stack CSolar::CreateParms at [ESP+0x20], built in retail's
    // order -- FUN_0052e0a0 on the CSimple head (+0x00..+0x58), +0x5c = 0, +0x60 = -1,
    // FUN_004cdc10 on +0x64 (+0x64..+0x70 and the count +0x94), +0x98 = 0, the EquipDescVector
    // (+0x9c) and the collision-group list (+0xac) constructed, ID_String::clear on +0xb8, byte
    // +0xbc = 0, clear on +0xc0, bytes +0xc4 / +0xc5 = 0, +0xc8 = 0, clear on +0xcc, +0xd0..+0xe0
    // = 0, and the anim list (+0xe4, FUN_0046ba60). Value-initialisation zeroes what retail
    // leaves unwritten (+0x74..+0x90); ReadObj fills the rest.
    CSolar::CreateParms Parms{};
    InitCreateParmsScratchRetail(&Parms);
    Parms.DockingCount = 0;
    Parms.VibeId = 0xffffffffu;
    Parms.SpaceCostume.PartIds[0] = 0;
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0;
    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0;
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // 0x00548b28..0x00548b57: ReadObj's bool is not looked at -- the archetype it returned
    // is. Parms.ObjDb (+0x04) is set to PTR_DAT_00613ed0 before that test.
    Archetype::Solar* OutArch = nullptr;
    CSolar::ReadObj(Reader, OutArch, Parms);
    Parms.ObjDb = GetClientObjDb();
    if (OutArch == nullptr)
    {
        // 0x00548d1f..0x00548d85: ~CreateParms, return false.
        return false;
    }

    // 0x00548b5d..0x00548b5f: the archetype's vt+0x0c, Archetype::Root::load.
    OutArch->load();

    // 0x00548b62..0x00548bf2: operator new(0x208) + the solar gameobj constructor. Retail has
    // no failure path after the allocation (a null ESI is dereferenced at 0x00548bf6).
    FClientGameobj* const Go = ClientGameobj_CreateSolar();

    // Slot 90 returns nothing; retail FUN_00548a20 (0x00548c03) goes on without a check.
    Go->create(OutArch);

    // 0x00548c09..0x00548c33: the object (+0x10) narrowed to a CSolar by
    // (ObjectClass & 0x303) == 0x303, else null, then CSolar::init with no null test.
    CObject* const Obj = Go->Object;
    CSolar* const Solar =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
            ? static_cast<CSolar*>(Obj)
            : nullptr;
    Solar->init(Parms);

    // 0x00548c39..0x00548c44: the archetype's companion (+0x04) vt+0x20 Load(1), no null test.
    OutArch->Companion->Load(1);

    // 0x00548c47..0x00548c61: FUN_004252d0(&equipment, 1), then FUN_00425480(get_index()).
    Archetype::LoadMountedEquipmentCompanions(&Parms.Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Go->get_index());

    // 0x00548c69..0x00548c7c: back to the section start, then the client half FUN_005485c0
    // (ring / burn_color / dock_script / voice / jump_effect / star, then the nebula).
    Reader.set_state(SectionStart);
    static_cast<FClientSolarGameobj*>(Go)->ReadIni(Reader);

    ClientObjDb_RegisterGameobj(Go); // 0x00548c83: FUN_00540c30(Go, 0)

    // 0x00548c88..0x00548c8f: Archetype::Root::Release; ~CreateParms follows, return true.
    OutArch->Release();
    return true;
}

void SolarSpheresApplyDetail(float Detail)
{
    // FUN_005488a0 (cdecl). The whole computation stays on the x87 stack (extended precision),
    // so it is done in double here.
    //
    // 005488a0..005488f0: T = Detail + Detail - 1.0, then FCOM 1.0 / TEST AH,0x41 / JNZ: only a
    // T above 1.0 is replaced by 1.0; otherwise FCOM 0.0 / TEST AH,0x5 / JP: only a T below
    // 0.0 is replaced by 0.0 (an unordered T passes both).
    double T = static_cast<double>(Detail) + static_cast<double>(Detail) - 1.0;
    if (T > 1.0)
        T = 1.0;
    else if (T < 0.0)
        T = 0.0;

    // 005488f0..00548910: ftol(3.0 * T + 7.0) -> EBX, then ftol(3.5 - (T + T)) -> [ESP+0x10].
    int const Tessellation = static_cast<int>(3.0 * T + 7.0);
    int const Level = static_cast<int>(3.5 - (T + T));

    // 00548919..0054892b: FUN_00540fc0 on bucket 2 (solars); walk ListNext (+0x14).
    int const SolarKind = 2;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&SolarKind); Go != nullptr;
         Go = Go->ListNext)
    {
        // 0054893b..00548963: the object (+0x10) narrowed to a CSolar by
        // (ObjectClass & 0x303) == 0x303, else null, and its type byte (+0xe0) tested
        // against 7; zero -> next. Solar buckets only hold solars, so the null is never read.
        CObject* const Obj = Go->Object;
        CSolar* Solar = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
            Solar = static_cast<CSolar*>(Obj);
        if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) == 0u)
            continue;

        // 00548969..005489a8: the same object narrowed to a CSimple ((ObjectClass & 3) == 3),
        // its engine instance (+0x04) queried for "1.11_ISphereMesh" through IEngine +0x78.
        CObject* Simple = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 3u) == 3u)
            Simple = Obj;
        ISphereMesh* Sphere = nullptr;
        GENRESULT const Result = DALib::Engine->QueryInstance(
            reinterpret_cast<void*>(static_cast<intptr_t>(Simple->EngineInstanceId)),
            Iids::SphereMesh, reinterpret_cast<void**>(&Sphere));
        if (Result < 0)
        {
            // 005489b3..005489c2: a failed query still releases whatever came back.
            if (Sphere != nullptr)
                Sphere->Release();
            continue;
        }

        // 005489c7..005489dd: Slot07(Tessellation), then Slot03(float(Level)) (FILD).
        Sphere->SetMaxTessellationLevel(Tessellation);
        Sphere->SetLodDistanceScale(static_cast<float>(Level));
        // 005489e0..005489f3.
        if (Sphere != nullptr)
            Sphere->Release();
    }
}
