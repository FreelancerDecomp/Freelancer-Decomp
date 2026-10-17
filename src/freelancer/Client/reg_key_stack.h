// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// A small stack of open registry keys that freelancer.exe keeps on the caller's stack
// (retail has no class name for it -- no vftable, no RTTI): a depth, then up to 32 HKEYs.
// Every user starts it with one entry, HKEY_CURRENT_USER, and opens (or creates) each path
// component on top of the previous one, so closing the stack closes the whole chain.
//
// Users: PageCharacterSelect's CharacterTable (0x00567350.., 0x00567ab0..), PageServerSelect's
// ServerTable (FUN_0056daa0, Create 0x0056e9d2..), the login history (FUN_005b62a0..), and the
// UIFastTable edge persistence it is handed to (FUN_0058c370 / FUN_0058c470).

#include <windows.h>

struct FRegKeyStack
{
    // The inline start every user spells (0x0056e9d2..0x0056e9f8): depth 0, FUN_005687e0 (a
    // no-op on the empty stack), then HKEY_CURRENT_USER pushed.
    FRegKeyStack();
    // CloseAll -- the EH-protected teardown at every exit (0x0056ea7d, 0x0056dbbc loop).
    ~FRegKeyStack();

    // FUN_005687c0 -- the top key, removed without closing it; 0 when the stack is empty.
    HKEY Pop();
    // FUN_005687e0 -- close every key, top first (HKEY_CURRENT_USER included).
    void CloseAll();
    // FUN_00568810 -- close the top key and remove it.
    void PopClose();
    // FUN_00568830 (RET 8) -- RegOpenKeyExA(top, SubKey, 0, Access) pushed on success. 0 when
    // the stack is empty, its top is 0, it is full (32), or the open failed.
    HKEY Open(char const* SubKey, REGSAM Access);
    // The RegCreateKeyExA twin of Open that FUN_0056daa0 / FUN_00567350 spell inline: class
    // "" (DAT_005c865c), no options, no security attributes. Pushes whatever key the call
    // returned when it succeeded; 0 on the same refusals as Open.
    HKEY Create(char const* SubKey, REGSAM Access);
    // FUN_005b6630 -- the top key without popping it, 0 when the stack is empty (also spelled
    // inline as the `[stack + depth * 4]` read of FUN_0058c370 / FUN_0058c470).
    HKEY Top() const;
    // FUN_005b6b90 (RET 8) -- RegSetValueExA(Top(), Name, 0, REG_SZ, Value, strlen + 1).
    LONG SetString(char const* Name, char const* Value);

    int Depth;       // +0x00
    HKEY Keys[32];   // +0x04
};

// DAT_006143b0 -- the .data pointer to s_ 0x005e1f64 "Software\Microsoft\Microsoft Games\
// Freelancer\1.0" that every user above passes as its first key. Never written; the listing's
// only references are its six reads: 0x00567373 / 0x00567ad0 (PageCharacterSelect),
// 0x0056dac3 / 0x0056e9fc (PageServerSelect), 0x005b62e4 / 0x005b6823 (login history).
extern char const* G_FreelancerRegKey;
