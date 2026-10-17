// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The named-script table a RoomObject keeps for its DALib::Anim script (retail freelancer.exe
// 0x00555a40..0x00555cd0, no source path string of its own). RoomControl holds one at +0x4c
// (FUN_00449260 / FUN_004496b0) and Gf::ObjectProp holds one at +0x4c (FUN_00445f40); the mesh
// loader FUN_0041c540 fills it from the .cmp's own file system.
//
// Layout (sizeof 0x18): +0x00 Count, +0x04 char** Names (malloc_strcpy copies),
// +0x08 float* Durations, +0x0c Script (the IAnimation2 script handle, -1 = none),
// +0x10 the script being enumerated (only valid inside Load), +0x14 the fill cursor.
struct FAnimScriptSet
{
    int Count;                 // +0x00
    char** Names;              // +0x04
    float* Durations;          // +0x08
    unsigned long Script;      // +0x0c
    unsigned long LoadScript;  // +0x10
    int FillCursor;            // +0x14

    // FUN_00555a40 (thiscall, no arguments) -- zero the table, both handles -1.
    void Init();
    // FUN_00555a70 -- the same stores as FUN_00555a40, in the ECX-relative form.
    void Reset();
    // FUN_00555a90 -- free every name, both arrays, and close the script through Anim +0x10.
    void Destroy();
    // FUN_00555b20 (thiscall, RET 4) -- the index of the name equal to Name (strcmp), or -1.
    int Find(char const* Name) const;
    // FUN_00555bf0 (thiscall, RET 4) -- take Script and, when it is not -1, count its names
    // (Anim +0x1c with FUN_00555b90), allocate both arrays and fill them (Anim +0x1c with
    // FUN_00555ba0). Returns +0x00.
    int Load(unsigned long NewScript);
    // FUN_00555c70 (thiscall, RET 8) -- Anim +0x24 (PlayScript) on Script with Names[Index].
    unsigned long Play(void* Root, int Index);
};

static_assert(sizeof(FAnimScriptSet) == 0x18, "AnimScriptSet sizeof 0x18");

// FUN_00555ca0 (thiscall, RET 4; ECX is not read) -- stop the playback *Playback through
// Anim +0x38, release it through Anim +0x28 and set it to -1. Nothing when it is already -1.
void AnimScriptSetStopPlayback(unsigned long* Playback);
