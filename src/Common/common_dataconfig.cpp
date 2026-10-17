// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// DataConfig -- retail DataConfig.cpp (startup 0x06308e60, shutdown 0x06309850). Declared
// in common_dataconfig.h.
#include "common_dataconfig.h"
#include "common_archetype.h"  // Archetype::Load* / Free / FreeExplosions
#include "common_fuses.h"      // FuseDB, PetalDB_*, WeaponModDB_*
#include "common_goods.h"      // ReadConstants, Loadout, GoodList_*, BaseDataList_*
#include "common_ini.h"        // INI_Reader
#include "common_misc.h"       // GetFileSysPath
#include "common_newchar.h"    // NewChar::TheDB, RtcSlider, pub::StateGraph
#include "common_reputation.h" // Reputation::Startup / Shutdown, costume descriptions
#include "common_universe.h"   // Universe::Startup / Shutdown
#include "DALib.h"             // DALib::Data
#include "dacom.h"             // FDUMP

namespace
{
    const char kDataConfigSource[] = "E:\\FL\\Scratch\\Source\\Common\\DataConfig.cpp";
    const unsigned long kPathChars = 0x104;

    // DAT_063fcee8 — DataConfig startup refcount. BSS 0 until first successful startup.
    int GDataConfigRefCount = 0;

    const char kFmtEquipmentEmpty[] =
        "%s(%d) : *** WARNING: EQUIPMENT file '%s' contains no new definitions.\n";
    const char kFmtSolarEmpty[] =
        "%s(%d) : *** WARNING: SOLAR file '%s' contains no new definitions.\n";
    const char kFmtShipsEmpty[] =
        "%s(%d) : *** WARNING: SHIPS file '%s' contains no new definitions.\n";
    const char kFmtAsteroidsEmpty[] =
        "%s(%d) : *** WARNING: ASTEROIDS file '%s' contains no new definitions.\n";
    const char kFmtExplosionsEmpty[] =
        "%s(%d) : *** WARNING: Explosion file '%s' contains no new definitions.\n";
    const char kFmtLoadoutsEmpty[] =
        "%s(%d) : *** WARNING: LOADOUTS file '%s' contains no new definitions.\n";

    int CallProgressCb(DataConfig::ProgressCB* Progress, char const* ValueString)
    {
        // listing 06308f3c: thiscall vtable[0], arg = get_value_string().
        return Progress->Slot00(ValueString);
    }

    void DispatchDataValue(INI_Reader& Reader, Archetype::ICliObjFactory* Factory)
    {
        char FullPath[kPathChars];

        if (Reader.is_value("universe"))
        {
            // Relative path — Universe::Startup does GetFileSysPath internally.
            Universe::Startup(Reader.get_value_string());
        }
        else if (Reader.is_value("constants"))
        {
            ReadConstants(Reader.get_value_string());
        }
        else if (Reader.is_value("equipment"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Archetype::LoadEquipment(FullPath, false, Factory) == 0)
            {
                FDUMP(0x100002, kFmtEquipmentEmpty, kDataConfigSource, 0x5c, FullPath);
            }
        }
        else if (Reader.is_value("solar"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Archetype::LoadSolar(FullPath, false, Factory) == 0)
            {
                FDUMP(0x100002, kFmtSolarEmpty, kDataConfigSource, 0x68, FullPath);
            }
        }
        else if (Reader.is_value("ships"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Archetype::LoadShips(FullPath, false, Factory) == 0)
            {
                FDUMP(0x100002, kFmtShipsEmpty, kDataConfigSource, 0x75, FullPath);
            }
        }
        else if (Reader.is_value("asteroids"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Archetype::LoadAsteroids(FullPath, Factory) == 0)
            {
                FDUMP(0x100002, kFmtAsteroidsEmpty, kDataConfigSource, 0x82, FullPath);
            }
        }
        else if (Reader.is_value("loadouts"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Loadout::Load(FullPath, false) == 0)
            {
                // 0630918e-063091ad: called through the FDUMP pointer with no test.
                FDUMP(0x100002, kFmtLoadoutsEmpty, kDataConfigSource, 0x8e, FullPath);
            }
        }
        else if (Reader.is_value("bodyparts"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            LoadBodypartDescriptions(FullPath);
        }
        else if (Reader.is_value("costumes"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            LoadCostumeDescriptions(FullPath);
        }
        else if (Reader.is_value("bases"))
        {
            BaseDataList_load();
        }
        else if (Reader.is_value("goods"))
        {
            GoodList_load(Reader.get_value_string());
        }
        else if (Reader.is_value("markets"))
        {
            BaseDataList_load_market_data(Reader.get_value_string());
        }
        else if (Reader.is_value("PetalDB"))
        {
            PetalDB_load(Reader.get_value_string());
        }
        else if (Reader.is_value("Groups"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            Reputation::Startup(FullPath);
        }
        else if (Reader.is_value("explosions"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            if (Archetype::LoadSimples(FullPath, false, Factory) == 0)
            {
                FDUMP(0x100002, kFmtExplosionsEmpty, kDataConfigSource, 0xd3, FullPath);
            }
        }
        else if (Reader.is_value("fuses"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            FuseDB::AddFuseINIFile(FullPath);
        }
        else if (Reader.is_value("WeaponModDB"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            WeaponModDB_load(FullPath);
        }
        else if (Reader.is_value("NewCharDB"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            // 063093e9-063093f9: TheDB->Load (+0x38), no null test.
            NewChar::TheDB->Load(FullPath);
        }
        else if (Reader.is_value("RtcSlider"))
        {
            GetFileSysPath(FullPath, Reader.get_value_string(), nullptr);
            RtcSlider::load(FullPath);
        }
    }
}

int __cdecl DataConfig::startup(char const* IniPath, Archetype::ICliObjFactory* Factory,
                                ProgressCB* Progress)
{
    // Retail: if DAT_063fcee8 != 0, increment and return 0 (no Data check).
    if (GDataConfigRefCount != 0)
    {
        GDataConfigRefCount += 1;
        return 0;
    }

    // 06308e7f-06308e95: the refcount is incremented before the test (open fail still
    // counts). No DALib::Data test -- the former guard was invented (CMN-S2 AIS 2026-10-14).
    GDataConfigRefCount = 1;

    INI_Reader Reader;
    bool bAbort = false;
    if (Reader.open(IniPath, false))
    {
        if (Reader.read_header())
        {
            do
            {
                // 06308ef5: the abort byte is tested before each section.
                if (bAbort)
                    break;

                if (Reader.is_header("Data") && Reader.read_value())
                {
                    do
                    {
                        // 06308f30: and before each value.
                        if (bAbort)
                            break;

                        // 06308f38-06308f5d: a non-zero callback answer sets the abort byte and
                        // jumps to the read_value at 06309432 -- the value is not dispatched, but
                        // the reader still advances once more before the loops leave.
                        if (Progress != nullptr &&
                            CallProgressCb(Progress, Reader.get_value_string()) != 0)
                        {
                            bAbort = true;
                            continue;
                        }

                        DispatchDataValue(Reader, Factory);
                    } while (Reader.read_value());
                }
            } while (Reader.read_header());
        }

        // After successful open (even on ProgressCB abort): state_graph binary.
        char StateGraphPath[kPathChars];
        GetFileSysPath(StateGraphPath, "AI\\state_graph.db", nullptr);
        pub::StateGraph::LoadFromFile(StateGraphPath);
    }

    return bAbort ? 1 : 0;
}

void __cdecl DataConfig::shutdown()
{
    // listing 06309850. DEC then JNZ. shutdown without startup: 0 → -1, skip teardown.
    GDataConfigRefCount -= 1;
    if (GDataConfigRefCount != 0)
        return;

    pub::StateGraph::Free();
    Loadout::Free();
    Archetype::Free();
    Archetype::FreeExplosions();
    FreeCostumeDescriptions();
    GoodList_destroy();
    PetalDB_destroy();
    BaseDataList_destroy();
    Universe::Shutdown();
    Reputation::Shutdown();
    FuseDB::Shutdown();
    WeaponModDB_destroy();
    // 06309894-0630989c: tail jump to TheDB->Shutdown (+0x14), no null test.
    NewChar::TheDB->Shutdown();
}
