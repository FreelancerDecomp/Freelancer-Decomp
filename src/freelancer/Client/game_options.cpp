// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_005792b0 and the option-table scatter loop at 0x005621c0 (inside FUN_005621a0).

#include "game_options.h"
#include "globals.h"
#include "login_mgr.h"    // G_Server (DAT_0067ecd0)
#include "player_spawn.h" // GetPlayerConnectionId (DAT_00673344)

#include "common_server.h" // IServer::SetInterfaceState (slot 77)

#include <cstring>

// The values themselves live in the options object: DisplaySettings::options (globals.h), .data
// 0x00679be8 = 0x00679bc0 + 0x28. FUN_005621a0 (LoadStartupConfig) scatters the table below into
// it before anything reads an option.

// The descriptor table at .data 0x006143b8, transcribed row for row. Rows are in the order the
// options menu would have listed them, which is NOT index order -- see the two comments below,
// both of which are retail's own quirks and are reproduced deliberately.
const FGameOptionDesc GGameOptionDescs[] =
{
    {0x00, "[OUTLINES]",                        0, 0},
    {0x01, "PHYS WIRES",                        0, 1},
    {0x02, "PHYS SPHERES",                      0, 1},
    {0x03, "SHIELDS",                           0, 1},
    {0x04, "PHANTOMS",                          0, 1},
    {0x05, "HOTSPOTS",                          0, 1},

    {0x06, "[INTERFACE]",                       1, 0},
    {0x07, "BUTTONS",                           0, 1},
    {0x08, "TEXT",                              1, 1},
    {0x09, "NEURONET",                          2, 1},
    {0x0a, "HUD",                               1, 1},
    {0x0c, "TARGET WINDOW LOCK",                0, 1},
    {0x0d, "AUTO OPEN TARGET WINDOW",           1, 1},
    {0x0e, "DRAW MOUSE",                        1, 1},
    {0x0f, "DISPLAY SHIP IDS",                  0, 1},
    {0x10, "DISPLAY INTERFACE TIMING",          0, 1},
    {0x11, "DISPLAY PLAYER STATS",              0, 1},
    {0x12, "SWITCHING TARGETS CLEARS MANEUVER", 1, 1},

    // Out of sequence in retail: index 0x0b sits here, after 0x12, not up with the other
    // interface toggles. Row order is menu order.
    {0x0b, "SHIP WIRES",                        1, 1},

    {0x13, "DRAW TEXT CONTROLS",                1, 1},
    {0x14, "DRAW BUTTON CONTROLS",              1, 1},
    {0x15, "DRAW STATIC CONTROLS",              1, 1},
    {0x16, "DRAW SCROLL BAR CONTROLS",          1, 1},
    {0x17, "INVERSE MOUSE",                     0, 1},

    // Not bracketed, yet flagged like a category header rather than a toggle. Retail's own
    // inconsistency; nothing reads the flag, so it changes nothing.
    {0x18, "LIGHTING",                          2, 0},

    {0x19, "[BACKGROUND]",                      1, 0},
    {0x1a, "STARS",                             1, 1},

    {0x1b, "[OBJECTS]",                         1, 0},
    {0x1c, "CHARACTERS",                        1, 1},
    {0x1d, "SHIPS",                             1, 1},
    {0x1e, "PROJECTILES",                       1, 1},
    {0x1f, "DEBRIS",                            1, 1},
    {0x20, "SOLAR",                             1, 1},
    {0x21, "ASTEROIDS",                         1, 1},
    {0x22, "DYN_ASTEROIDS",                     1, 1},
    {0x23, "PLANETS",                           1, 1},
    {0x24, "SUN",                               1, 1},

    {0x25, "[EFFECTS]",                         1, 0},
    {0x26, "PARTICLES",                         1, 1},
    {0x27, "NEBULA",                            1, 1},
    {0x28, "SUN_EFFECTS",                       1, 1},
    {0x29, "FLASHLIGHTS",                       1, 1},
    {0x2a, "TRADE_LANES",                       1, 1},
    {0x2b, "SHADOWS",                           1, 1},
    {0x2c, "ASTEROID_EXT",                      2, 1},

    {0x2d, "[AUDIO]",                           1, 0},

    // Retail really does list index 0x2f twice, here and three rows down, with 0x2e wedged
    // between them. Whatever this row was meant to be, both copies write the same 0, so the
    // duplicate is invisible at runtime -- but it is why the table has 71 rows for 70 options.
    {0x2f, "VOL_VOICE",                         0, 1},
    {0x2e, "VOL_MUSIC",                         0, 1},
    {0x2f, "VOL_VOICE",                         0, 1},

    {0x30, "VOL_SFX",                           0, 1},
    {0x31, "VOL_INTERFACE",                     0, 1},
    {0x32, "VOL_AMBIENCE",                      0, 1},
    {0x33, "VOICE PROFILE OFFSET",              0, 1},
    {0x34, "PITCH_BEND_SOUNDS",                 1, 1},

    {0x35, "[PLAYER]",                          0, 0},
    {0x36, "AUTO_AIM",                          0, 1},
    {0x37, "DISABLE_COLLIDE",                   0, 1},
    {0x38, "SURFACE_HIT_EFFECTS",               1, 1},
    {0x39, "SPLASH_HIT_EFFECTS",                0, 1},

    {0x3a, "[MISC]",                            0, 0},
    {0x3b, "AMBIENT",                           0, 1},
    {0x3c, "DETAIL_LEVELS",                     0, 1},
    {0x3d, "CHAR_PLACES",                       0, 1},
    {0x3e, "LIKE_PETAL",                        0, 1},
    {0x3f, "LIGHTS_DEBUG",                      0, 1},
    {0x40, "ALCHEMY_LOD",                       1, 1},
    {0x41, "COMM_LOG",                          0, 1},

    {0x42, "[ALCHEMY_DEBUG]",                   1, 0},
    {0x43, "BATCH_PARTICLES",                   0, 1},
    {0x44, "USE_DYN_VB",                        1, 1},
    {0x45, "COPY_TO_DYN_VB",                    1, 1},
};

const int GGameOptionDescCount = static_cast<int>(sizeof(GGameOptionDescs) / sizeof(GGameOptionDescs[0]));

// Retail's loop bound is a byte count: `while (Off < 0x470)` over 0x10-byte records, so exactly
// 0x470 / 0x10 = 71 rows. If this ever fires, a row was lost or gained in transcription.
static_assert(sizeof(GGameOptionDescs) / sizeof(GGameOptionDescs[0]) == 0x470 / 0x10,
              "the option descriptor table must have 71 rows, as retail's 0x470 byte span does");

// FUN_005792b0 (.text:005792b0-005792c3): `CMP EAX,0x46; JGE -> return 0; MOV EAX,[EAX*4 +
// 0x00679be8]`. The compare is signed, so a negative index reads the options-object fields in
// front of the array, exactly as retail does. No caller in the PE passes one.
int GetGameOption(int Index)
{
    if (Index >= kGameOptCount)
        return 0;

    const int* const Values = reinterpret_cast<const int*>(&g_display) + 10;
    return Values[Index];
}

char const* GetGameOptionName(int Index)
{
    for (int Row = 0; Row < GGameOptionDescCount; ++Row)
    {
        if (GGameOptionDescs[Row].Index == Index)
            return GGameOptionDescs[Row].Name;
    }

    return nullptr;
}

namespace
{
    // .data:00612274. See the header's "Session/game-mode flags word" section. Its initialised
    // value in the PE is 3 (bits 1 and 2 set); the writers are FUN_004c8120 / FUN_004c8190 only.
    unsigned GSessionFlags = 3;
}

void SetGameSessionFlags(unsigned Mask, bool bSet)
{
    // 0x004c8120..0x004c8155: `if (param_2 == 0) flags &= ~Mask; else flags |= Mask;`
    if (!bSet)
        GSessionFlags &= ~Mask;
    else
        GSessionFlags |= Mask;

    // 0x004c815a..0x004c817e: the new word in the first dword of a 0x80-byte local, sent with
    // IServer slot 77 (+0x134) SetInterfaceState(DAT_00673344, &local, 4) when DAT_0067ecd0 is
    // set. The rest of the local is never written.
    unsigned char Buffer[0x80];
    memcpy(Buffer, &GSessionFlags, sizeof(GSessionFlags));
    if (G_Server != nullptr)
        G_Server->SetInterfaceState(GetPlayerConnectionId(), Buffer, 4);
}

void AssignGameSessionFlags(unsigned Value)
{
    // 0x004c8190: unconditional overwrite, no broadcast in retail either.
    GSessionFlags = Value;
}

bool TestGameSessionFlags(unsigned Mask)
{
    // 0x004c8110: `(DAT_00612274 & Mask) != 0`.
    return (GSessionFlags & Mask) != 0;
}
