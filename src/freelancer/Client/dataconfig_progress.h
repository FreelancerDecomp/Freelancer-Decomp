// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

// Retail FUN_005b0c30 (Client\main.cpp) -- the client's own pass over freelancer.ini after
// DataConfig::startup: the [Data] keys Common.dll leaves to the client (fonts, rich_fonts,
// sounds, voices, effects, effect_shapes, debris, intro, HUD, igraph), the [Resources] DLLs,
// then audio\SoundCFG.ini. Also FUN_00564b30, the intro.ini [Images] reader it calls.
// DataConfig::startup's progress object is not here: it is InitializeEngine's DataConfigPump
// (engine_init.cpp).

// One intro.ini string field: {length, text}, stride 0x48 in retail. FUN_00564b30 hands
// FUN_00417c50 a capacity of 0x40 and stores the returned length in front of the text; a null
// value stores length 0 and a zero first byte. Each field's {length, text[0]} pair is zeroed by
// its own static initializer (LAB_00564a60 .. LAB_00564ae0).
struct FIntroPathString
{
    unsigned Length;  // +0x00
    char Text[0x44];  // +0x04  (FUN_00417c50 capacity 0x40)
};
static_assert(sizeof(FIntroPathString) == 0x48, "intro.ini string field stride is 0x48");

// FUN_00564b30 ([Images] of intro.ini). Retail keeps these as separate .data globals; the port
// groups them. ScreenType/ScreenExit/ScreenReset/Screen1-4 are write-only in the PE: each has
// exactly three references (the parse store, the static-init zero, the atexit clear) and no
// reader, so nothing in the port may consume them either.
struct FIntroConfigState
{
    FIntroPathString TgaPath;     // DAT_0067c188 / DAT_0067c18c  "TgaPath"
    FIntroPathString ScreenType;  // DAT_0067c140 / DAT_0067c144  "ScreenType"
    FIntroPathString ScreenExit;  // DAT_0067c0f8 / DAT_0067c0fc  "ScreenExit"
    FIntroPathString ScreenReset; // DAT_0067c0b0 / DAT_0067c0b4  "ScreenReset"
    FIntroPathString Screen[4];   // DAT_0067c068 / c020 / bfd8 / bf90  "Screen1".."Screen4"
    // DAT_0067bf48 / DAT_0067bf4c -- after a successful parse 00564e5c copies
    // "FRONT_freelancerlogo.tga" (bounded at 0x3f chars) here; the title screen pushes it at
    // 005738eb.
    FIntroPathString Logo;
    // DAT_0067bf40 -- (rand() * 3) >> 15 after a successful parse (00564e9c); 005738xx picks
    // Intro1_Base / Intro2_Base / Intro3_Base from it.
    int IntroBasePick;
    // DAT_00614a9c -- PE init 1; only a "bDrawScripts" key rewrites it (00564e20).
    bool bDrawScripts;
};

FIntroConfigState const* GetIntroConfigState();

// FUN_005b0c30, cdecl(ini path); InitializeEngine 0x005b1caa passes DAT_00679d04. Always true.
bool LoadClientIniData(char const* IniPath);
