// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "menu_ui_init.h"

#include "input_manager.h"
#include "menu_sound_arch.h"
#include "menu_ui_widgets.h"
#include "texture_shapes.h"
#include "win3d_engine_submit.h"

#include "client_state.h"
#include "common_id.h"
#include "globals.h"
#include "DALib.h"
#include "soundstreamer.h"
#include "dacom.h"
#include "space_camera.h"

#include <io.h>
#include <string.h>
#include <windows.h>

namespace
{
    static const int kUiSoundTableCount = 0x2d;

    // DAT_006149e8 — 0x2d nicknames (listing .data:006149e8..00614a98).
    static const char* kUiSoundNames[kUiSoundTableCount] = {
        "ui_select_item",                    // 0
        "ui_item_select",                    // 1
        "ui_window_open",                    // 2
        "ui_open_infocard",                  // 3
        "ui_filter_select",                  // 4
        "ui_nav_map_expand",                 // 5
        "hud_zoom_in",                       // 6
        "hud_zoom_out",                      // 7
        "ui_nav_map_fade",                   // 8
        "ui_nav_map_submenu",                // 9
        "hud_waypoint",                      // 10
        "ui_typing_characters",              // 11
        "ui_nn_expansioncontraction",        // 12
        "ui_begin_scan",                     // 13
        "ui_end_scan",                       // 14
        "ui_new_story_star",                 // 15
        "ui_load_cargo",                     // 16
        "ui_equip_detach",                   // 17
        "ui_equip_mount01",                  // 18
        "ui_equip_mount02",                  // 19
        "ui_equip_mount03",                  // 20
        "ui_equip_mount04",                  // 21
        "ui_equip_mount05",                  // 22
        "hud_weapon_select",                 // 23
        "hud_subtarget",                     // 24
        "ui_incoming_missile",               // 25
        "ui_execute_transaction",            // 26
        "ui_motion_swish",                   // 27
        "ui_toggle_mouse_flight",            // 28
        "ui_rollover",                       // 29
        "ui_voice_test",                     // 30
        "ui_effects_test",                   // 31
        "ui_music_test",                     // 32
        "ui_interface_test",                 // 33
        "ui_ambiance_test",                  // 34
        "ui_target_change",                  // 35
        "ui_target_expand_brackets",         // 36
        "ui_buy_commodity",                  // 37
        "ui_receive_money",                  // 38
        "ui_gain_level",                     // 39
        "ui_use_nanobots_success",           // 40
        "ui_use_shield_battery_success",     // 41
        "ui_use_nanobots_failure",           // 42
        "ui_use_shield_battery_failure",     // 43
        "hud_comm_window",                   // 44
    };

    // DAT_0067a4a8 -- 0x2d cached SoundArch pointers (.bss, zeroed again by FUN_00564520).
    void* GUiSoundTable[kUiSoundTableCount] = {};

}

void InitUiSoundTable()
{
    // FUN_00564520 — zero DAT_0067a4a8 .. 0x2d pointers.
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
        GUiSoundTable[Index] = nullptr;
}

int FindUiSoundSlot(const char* Name)
{
    // FUN_00564610 (cdecl): DACOM stricmp(DAT_006149e8[Index], Name) for Index 0..0x2c, the
    // first match wins; -1 otherwise. No test of Name or of the table entries.
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (_stricmp(kUiSoundNames[Index], Name) == 0)
            return Index;
    }
    return -1;
}

const char* GetUiSoundSlotName(unsigned int Slot)
{
    if (Slot >= static_cast<unsigned int>(kUiSoundTableCount))
        return nullptr;
    return kUiSoundNames[Slot];
}

void PlayUiSoundSlot(unsigned int Slot)
{
    // FUN_00564570(Slot): no range check and no init latch. An empty cache slot is filled from
    // FUN_0042ae40(CreateID(DAT_006149e8[Slot])) and AddRef'd through the archetype's vtable+0xc,
    // with no null test on the lookup; then FUN_004285f0(Arch, 0, 0).
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    PlayClientSoundArchIfIdle(GUiSoundTable[Slot], nullptr, nullptr);
}

void PlayUiSoundSlotFlag1(unsigned int Slot)
{
    // FUN_00564650(Slot): the lazy fill of FUN_00564570 (00564654..0056468f), then a 0x18-byte
    // play key on the stack -- +0x00 and +0x08..+0x14 zero, the +0x04 byte (flags & 0xf8) | 1
    // (00564692..005646c8) -- and FUN_004285f0(Arch, &Key, 0).
    // PORT: retail keeps bits 3..7 of that byte and the dword's upper three bytes from whatever
    // was on the stack; the port zeroes them.
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    unsigned Key[6] = {0, 1, 0, 0, 0, 0};
    PlayClientSoundArchIfIdle(GUiSoundTable[Slot], Key, nullptr);
}

void PlayOrRewindUiSoundSlot(unsigned int Slot)
{
    // FUN_005645c0(Slot): the same lazy fill as FUN_00564570 (005645c4..005645fc), then
    // FUN_00428640(Arch, 0, 0). No range check, no init latch.
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    // 005645ff..00564606: FUN_00428640(Arch, 0, 0) -- rewind a live source of this archetype,
    // or start a new one. It never stops anything.
    PlayOrRewindClientSoundArch(GUiSoundTable[Slot], nullptr, nullptr);
}

void StopUiSoundSlot(unsigned int Slot)
{
    // FUN_005646e0(Slot): the cached archetype DAT_0067a4a8[Slot] as it stands (no lazy fill,
    // no range check), tail-jumped into FUN_004285c0.
    StopClientSoundArch(GUiSoundTable[Slot]);
}

void PlayUiRolloverSound()
{
    // FUN_00564700: only while the intro menu is the current state (FUN_005b0880) or the local
    // server tick is held (FUN_0042d730). Slot 0x1d ("ui_rollover", DAT_00614a5c / cache
    // DAT_0067a51c) is filled lazily like FUN_00564570, then FUN_00428680(Arch, 0, 0, 0) --
    // a plain start, not the if-idle variant.
    if (!IsClientIntroEnvironment() && !IsClientServerTickPaused())
        return;
    unsigned int const Slot = 0x1d;
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    PlayClientSoundArch(GUiSoundTable[Slot], nullptr, nullptr, 0);
}

bool IsAnyUiSoundPlaying()
{
    // FUN_00564760: ESI from DAT_0067a4a8 up to 0x0067a55c (JL, 4 bytes a step); a null slot is
    // skipped (0056476a), a non-null one goes to FUN_00428000 and a non-zero count answers 1
    // (00564777 -> 00564788). The walk running out answers 0 (00564784).
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (GUiSoundTable[Index] == nullptr)
            continue;
        if (CountClientSoundArchSources(GUiSoundTable[Index]) != 0)
            return true;
    }
    return false;
}

// FUN_00564540 -- walk the whole DAT_0067a4a8[0x2d] cache (end sentinel 0x0067a55c), release
// every non-null archetype reference through its vtable+0x10 (ReleaseClientSoundArch) and null
// the slot. No init latch: the table is static storage, zero before FUN_00564520 ever runs.
void ShutdownUiSoundTable()
{
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (GUiSoundTable[Index] == nullptr)
            continue;
        ReleaseClientSoundArch(GUiSoundTable[Index]);
        GUiSoundTable[Index] = nullptr;
    }
}
