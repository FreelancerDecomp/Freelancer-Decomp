// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// AnimDB — Common.dll animation-registration table (common_animdb.cpp).
// Retail exports two registration families: Add(int)/Rem(int) for freelancer.exe (muzzle
// joint, bay, gates) and Add(VChannel*)/Rem(VChannel const*) for per-frame joint channels,
// plus Count/Shutdown/Update.

namespace AnimDB {

// Retail RTTI ".?AUVChannel@AnimDB@@": an abstract struct with no data and two __thiscall
// slots. AnimDB::Update calls slot 0 on every registered channel (.text:0627def0..0627def6:
// MOV ECX,[node+8] / MOV EAX,[ECX] / PUSH Dt / CALL [EAX]); Add(VChannel*) calls slot 1
// (.text:0627d4df..0627d4e3 / 0627d601..0627d603 / 0627d62c..0627d62e: CALL [EDX+4]).
// No virtual destructor: retail never deletes a channel through this type. Known derived
// classes: CEGun::JointChannel (Common, vftable .rdata:0639d200); freelancer.exe carries one
// more (its RTTI base list at .rdata:005eda74).
struct VChannel
{
    virtual void Tick(float Dt) = 0;   // slot 0 (+0x00)
    virtual int GetId() const = 0;     // slot 1 (+0x04): engine instance, -1 when unbound
};

COMMON_API void __cdecl Add(int PlaybackId);                 // 0x0627d8a0 ?Add@AnimDB@@YAXH@Z
COMMON_API void __cdecl Add(VChannel* Chan);                 // 0x0627d4a0 ?Add@AnimDB@@YAXPAUVChannel@1@@Z
COMMON_API void __cdecl Rem(int PlaybackId);                 // 0x0627dc80 ?Rem@AnimDB@@YAXH@Z
COMMON_API void __cdecl Rem(VChannel const* Chan);           // 0x0627d680 ?Rem@AnimDB@@YAXPBUVChannel@1@@Z
COMMON_API unsigned int __cdecl Count();                     // 0x0627e480 ?Count@AnimDB@@YAIXZ
COMMON_API void __cdecl Shutdown();                          // 0x0627d470 ?Shutdown@AnimDB@@YAXXZ
COMMON_API void __cdecl Update(float Dt);                    // 0x0627dda0 ?Update@AnimDB@@YAXM@Z

} // namespace AnimDB
