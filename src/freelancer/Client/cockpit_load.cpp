// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cockpit_load.h"

#include "player_spawn.h"
#include "common_archetype.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "hud_element_list.h"
#include "space_camera.h"

#include <cstddef>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    static const unsigned kShipObjectClassFlags = 0x503U;

    // .rdata 0x005e07bc -- the fallback when the ship archetype names no cockpit.
    static const char kDefaultCockpitIniRel[] = "cockpits\\cockpit.ini";
    // .rdata 0x005e0704 and the line FUN_0054ab40 reports the fallback from.
    static const char kSpaceCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp";
    static const int kNoCockpitWarningLine = 0x5d5;

    // Same `- 8` as everywhere else: the watcher holds the Watchable, eight bytes into the
    // IObjRW.
    const int kWatchableOffset = 8;

    // Retail CEqObj offsets used by FUN_00543350.
    struct FPlayerShipView
    {
        void* Vtable;
        unsigned char PadTo4c[0x48];
        unsigned FlagsAt4c;
        unsigned char PadTo88[0x38];
        Archetype::Root* Archetype;
    };

    static_assert(offsetof(FPlayerShipView, FlagsAt4c) == 0x4c, "CEqObj+0x4c class flags");
    static_assert(offsetof(FPlayerShipView, Archetype) == 0x88, "CObject+0x88 archetype");

    // Archetype::CliShip companion — cockpit ini path at retail +0x60.
    struct FCliShipCompanionView
    {
        void* Vtable;
        unsigned char PadTo60[0x5C];
        char* CockpitIniPath;
    };

    static_assert(offsetof(FCliShipCompanionView, CockpitIniPath) == 0x60,
                  "CliShip+0x60 cockpit ini path");

    // player_spawn.cpp's fallback when it has no manager object; never bound.
    static FCockpitMeshHolder GCockpitMeshHolder;

    // FUN_004c7290 — .data:00673560.
    bool GCockpitResourceStringLoading = false;


    // FUN_00543350 (thiscall on the IObjRW, RET 0). The CEqObj is the IObjRW's +0x10 (retail
    // tests `this`, then `this + 0xc` -- the CBase subobject -- then the pointer it reads at
    // +0x10), and it counts only when its class word at +0x4c carries all of 0x503. The
    // archetype at +0x88 is read from whatever survived: a failed test leaves EAX null and
    // retail dereferences it anyway (0x00543375), so no guard is added here either.
    Archetype::Root* ShipArchetypeOf(void* ObjRW)
    {
        FPlayerShipView* EqObj = nullptr;
        if (ObjRW != nullptr)
        {
            unsigned char* const Base = static_cast<unsigned char*>(ObjRW) + 0x0c;
            if (Base != nullptr)
            {
                FPlayerShipView* const Candidate = *reinterpret_cast<FPlayerShipView**>(Base + 4);
                if (Candidate != nullptr &&
                    (Candidate->FlagsAt4c & kShipObjectClassFlags) == kShipObjectClassFlags)
                    EqObj = Candidate;
            }
        }
        return EqObj->Archetype;
    }
}

const FCockpitMeshHolder& GetCockpitMeshHolder()
{
    return GCockpitMeshHolder;
}

bool InitPlayerCockpitSlice()
{
    // 0x0054acc7: the player's IObjRW -- the watcher at DAT_00679744 minus eight, or null.
    void* const Watched = GetPlayerShipWatcher();
    void* const ObjRW =
        (Watched != nullptr) ? static_cast<unsigned char*>(Watched) - kWatchableOffset : nullptr;

    // 0x0054acd7 .. 0x0054ace2: FUN_00543350, then archetype +0x04 (the CliShip companion) and
    // its +0x60, the cockpit path. Retail tests only the first character -- the pointer itself
    // is never null-tested.
    Archetype::Root* const Arch = ShipArchetypeOf(ObjRW);
    const char* const CockpitPath =
        reinterpret_cast<FCliShipCompanionView*>(Arch->Companion)->CockpitIniPath;

    char FullPath[MAX_PATH];
    if (CockpitPath[0] == '\0')
    {
        // 0x0054ace6: warn, and fall back to the stock cockpit.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kSpaceCpp, kNoCockpitWarningLine,
              "ship has no cockpit definition");
        GetFileSysPath(FullPath, kDefaultCockpitIniRel, nullptr);
    }
    else
    {
        // 0x0054ad19.
        GetFileSysPath(FullPath, CockpitPath, nullptr);
    }

    // 0x0054ad25 .. 0x0054ad5d: the four cockpit camera loaders, same path, in order -- the
    // first-person one (FUN_005173c0) is what loads the cockpit mesh.
    SpaceCameraLoadCockpit(FullPath);

    // 0x0054ad62: FUN_004c73d0.
    LoadHudElementsFromIni();

    // 0x0054ad67.. (+0xbaa, RefreshLoadingProgress, FUN_00549bc0(this, 0, 0)) continue in the
    // space init itself (space_state.cpp InitSpaceState).
    return true;
}

void SetCockpitResourceStringLoading(bool bLoading)
{
    GCockpitResourceStringLoading = bLoading;
}

bool IsCockpitResourceStringLoading()
{
    return GCockpitResourceStringLoading;
}
