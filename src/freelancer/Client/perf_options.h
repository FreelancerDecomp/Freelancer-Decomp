// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// PerfOptions.cpp (retail source path E:\FL\Scratch\Source\Client\PerfOptions.cpp).
//
// Retail keeps a process-wide registry of "perf options": named float knobs registered from
// static initializers (FUN_005b3520), persisted to <user data>\PerfOptions.ini and applied
// through per-option callbacks. Entry points:
//
//   FUN_005b3520  register            FUN_005b36b0  find by name
//   FUN_005b3770  find by IDS id      FUN_005b3510  read one of the 7 shared float slots
//   FUN_005b38d0  load + apply        FUN_005b3ec0  save
//   FUN_005b40f0  apply everything changed since the last apply
//   FUN_005b4050  clear the registry  FUN_005b3860  max_submitted_sounds accessor
//
// Call order in retail InitializeEngine (FUN_005b1300 @ 005b168e):
//   LoadPerfOptions("PerfOptions.ini", &display)
//   if (!ok) { FUN_004a9380(); ApplyChangedPerfOptions(); }
//   SavePerfOptions("PerfOptions.ini")

struct FPerfDisplaySettings
{
    int Width;      // retail display struct +0x08
    int Height;     // +0x0c
    int ColorBpp;   // +0x10
    int DepthBpp;   // +0x14
};

// Retail option record is 0x1c bytes: vtable, name, IDS name, IDS info, value, applied,
// callback. The port drops the vtable: retail's generic class (vtable 005d6d50) forwards
// slot 1 to the +0x18 cdecl callback, and every other option's slot 1 is that option's own
// apply body, so one function pointer covers both. Slot 0 is a no-op for every option in
// the PE, so the "value parsed" hook is dropped too. See docs/re/deviations.md, PerfOptions.
struct FPerfOption
{
    char const* Name;           // +0x04
    int         IdsName;        // +0x08
    int         IdsInfo;        // +0x0c
    float       Value;          // +0x10  last parsed / assigned
    float       Applied;        // +0x14  last applied; -1 means "never" (retail 0xbf800000)
    void        (*Apply)(float);
};

// FUN_005b36b0 / FUN_005b3770 — exact name match, as in retail (byte compare, not _stricmp).
FPerfOption* FindPerfOption(char const* Name);
FPerfOption* FindPerfOptionByIds(int IdsName);
unsigned GetPerfOptionCount();
FPerfOption* GetPerfOptionAt(unsigned Index);

// FUN_005b3510 — the seven shared floats at DAT_0067ed10 that some applies write into.
float GetPerfOptionSlot(int Index);

// FUN_005b40f0 — apply every option whose Value drifted from Applied.
void ApplyChangedPerfOptions();

// FUN_005b38d0 / FUN_005b3ec0. FileName is a bare name; both prepend GetUserDataPath().
// Save takes only the name and always writes [Display] from the options object g_display
// (DAT_00679bc8..bd4).
bool LoadPerfOptions(char const* FileName, FPerfDisplaySettings* Display);
void SavePerfOptions(char const* FileName);

// The first-run seed FUN_004a9380 is NnPreferences::SeedGraphicsDefaults (nn_preferences.h):
// optlist walk on a bare NN_Preferences, this probe, then FUN_004b0370's row cascade.
// FUN_004a92a0 -- the machine's graphics score in {0.25, 0.5, 0.6, 0.75, 1.0}.
float ProbeMachineGraphicsLevel();

bool GetSkipMachineWarnings();
void SetSkipMachineWarnings(bool bSkip);

// FUN_005b3860 (DAT_00615c6c) — the mixer's voice budget, default 32.
int GetMaxSubmittedSounds();

// Simple globals the option callbacks write; kept here until their consumers are ported.
bool GetPerfEnableTooltips();           // DAT_00615c68
bool GetPerfEnableRollover();           // DAT_00615c69
bool GetPerfCockpit();                  // DAT_0067ed44
bool GetPerfAutohideManeuverBar();      // DAT_0067ed46
bool GetPerfSkipTransitionScripts();    // DAT_0066d304
bool GetPerfAdvancedGraphics();         // DAT_0067ed3c
int  GetPerfGeneralGraphics();          // DAT_00615c64
int  GetPerfDefaultGraphics();          // DAT_0067ed40
float GetPerfSpaceDustDetail();         // DAT_00614080
float GetPerfGenericEffectPerf();       // DAT_006129c8
