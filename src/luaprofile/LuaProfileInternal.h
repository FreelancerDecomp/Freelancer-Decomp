// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "luaprofile.h"

// luaprofile.dll internals. The ABI other modules see is src/include/luaprofile.h.

static constexpr unsigned long kLuaObjectSize = 0x28;
static constexpr unsigned long kAggDescSize = 0x14;
static constexpr unsigned long kFactorySize = 0xC;
static constexpr unsigned int kRegisterPriority = 0x40000000u;

static constexpr char kComponentName[] = "ILua";
static constexpr unsigned int kIidComponentFactoryBytes = 23;

static constexpr char kInnerILua[] = "ILua";
static constexpr char kInnerProfileParser[] = "IProfileParser";
static constexpr char kInnerAggregate[] = "IAggregateComponent";

// The factory takes dacom.h's FAGGDESC14 (kAggDescSize); +0x10 Value is not read.

FL_LAYOUT_ASSERT(sizeof(FAGGDESC14) == kAggDescSize, "retail AGGDESC size 0x14");
FL_LAYOUT_ASSERT(sizeof(Iids::ComponentFactory) == kIidComponentFactoryBytes, "cmpsb ECX=0x17");
FL_LAYOUT_ASSERT(sizeof(kComponentName) == 5, "ILua + NUL");

struct FSectionHandle
{
    FSectionHandle* Next;
    int LuaRef;
};

FL_LAYOUT_ASSERT(sizeof(FSectionHandle) == 8, "section handle extent");
