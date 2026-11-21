// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

/*
** luaprofile: retail FUN_06803340 alloc 0x7C, FUN_06802830 stack 0x800.
** TObject 16 × STACK_UNIT 128 = 0x800; last = base+0x7f0.
** Not PUC-Rio — layout checks only.
*/

#include "lobject.h"
#include "lstate.h"

/* Retail's 32-bit layout; the port-only x64 build (docs/x64.md) has 8-byte pointers. */
#if defined(_M_IX86)
typedef char LuaStateSizeCheck[(sizeof(struct lua_State) == 0x7C) ? 1 : -1];
#endif
typedef char LuaTObjectSizeCheck[(sizeof(TObject) == 16) ? 1 : -1];
