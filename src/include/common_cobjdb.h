// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_id.h"

// IObjDB — client inspect object database.
//
// Client (freelancer.exe): static DAT_00678a2c via PTR_DAT_00613ed0
//   (RTTI Object::IObjDBImpl, vtable .rdata:005df8ac). Port: GetClientObjDb()
//   in freelancer Client/client_objdb.cpp.
// Server: retail StarSystem (RTTI .?AVStarSystem@@, hierarchy StarSystem ->
//   IObjDB, single inheritance) is the per-system IObjDB. Its constructor
//   stores this base's vtable (.rdata:06d67cec) first and its own
//   (.rdata:06d67cd0) last. Port: class StarSystem in server_sysdb.h.
//
// There is no CObjDB class in Common.dll. CleanUpDatabases @ RVA 0xa4870 is a
// 5-byte JMP to Reputation::Vibe::FreeDynamic (see common_reputation).
// CSimple::connect only stores the pointer; objects enter the client DB through
// FUN_00540c30 / Gameobj registration (ClientObjDb_Register).

struct IObjRW;
namespace Universe { struct ISystem; }

// Retail Object::IObjDBImpl vtable (.rdata:005df8ac) — four __thiscall slots.
// Common CEScanner / CAsteroidSystem / CSimple call these through raw void**.
struct IObjDB
{
    // +0x00 LAB_00541d60 — walk type-bucket lists; IterState =
    //   {Cursor*, ListHead*, Mask}. Mask bits select which lists to visit.
    virtual IObjRW* __thiscall GetNext(void* IterState) = 0;

    // +0x04 LAB_00542130 — find Gameobj by object id (arg = unsigned* Id).
    virtual IObjRW* __thiscall FindById(unsigned const* ObjectId) = 0;

    // +0x08 LAB_005422c0 → JMP FUN_005556a0: return DAT_00679954
    // (Universe::ISystem* set by EnterStarSystem FUN_005545a0).
    virtual Universe::ISystem* __thiscall GetZoneDb() = 0;

    // +0x0c LAB_005422d0 — if (TypeMask & 0xfdff) return DAT_00678ae0 else 0.
    // DAT_00678ae0 = NewSpatial("BSP") ISpatialPartition*; Fill = vtable +0x1c.
    //
    // NOT pure. IObjDB's own vtable (Server.dll .rdata:06d67cec, COL
    // .rdata:06d6cf68 -> .?AUIObjDB@@) holds _purecall (.text:06d5fff0) in
    // slots 0..2 and a real body in slot 3: LAB_06d06e80,
    //   .text:06d06e80  33c0    XOR EAX,EAX
    //   .text:06d06e82  c20400  RET 0x4
    // i.e. the base answers "no spatial bucket" for any mask. Both known
    // implementations (Object::IObjDBImpl in freelancer.exe, StarSystem in
    // Server.dll) override it.
    virtual void* __thiscall GetBucket(unsigned TypeMask)
    {
        (void)TypeMask;
        return nullptr;
    }
};

class INI_Reader;

// Retail ?FindSolarRep@SolarReps@@YAHABUID_String@@@Z (.text:062b8a80): a global-namespace
// SolarReps, taking the nickname as an ID_String. Returns Id.Id when Reputation::Vibe::Verify
// accepts it, else -1.
namespace SolarReps
{
    COMMON_API int __cdecl FindSolarRep(ID_String const& Id);
}

namespace Reputation
{
    namespace SolarReps
    {
        // FUN_062b8de0 — walk system INI [Object] sections and Vibe::Alloc each nickname
        // (FUN_062b8c20 / FUN_062b8ab0). Retail runs this once per System file= open inside
        // LoadUniverseIni, before Zone/Object universe parse (then INI_Reader::reset).
        // Internal to Common.dll: retail exports no symbol for it.
        void __cdecl LoadObjectVibesFromSystemIni(INI_Reader& Reader);
    }
}
