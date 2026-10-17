// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- the behavior type registry: the twenty-one BehaviorTypeDesc descriptors and
// their load-time registry (GBehaviorRegistryInit, dynamic initialisation -- keep this TU's
// globals together and in this order), BehaviorTypes::get_behavior_id/get_behavior_name and
// CREATE_BEHAVIOR.
// CMN-SPLIT stage 1 (2026-10-14): split out of common_iface.cpp unchanged.

#include "common_iface.h"
#include "common_behavior.h" // class Behavior; also FormationBehavior, the id-10 registry entry

// The twenty concrete behavior classes the id 0..20 registry entries construct. BehaviorTypes'
// create_behavior() has to see every one of them, so this is the one translation unit that
// includes the whole family. FormationBehavior lives in common_behavior.h above.
#include "common_buzzbehavior.h"
#include "common_cruisebehavior.h"
#include "common_delaybehavior.h"
#include "common_dockbehavior.h"
#include "common_drasticevadebehavior.h"
#include "common_evadebehavior.h"
#include "common_facebehavior.h"
#include "common_fleebehavior.h"
#include "common_followbehavior.h"
#include "common_freeflightbehavior.h"
#include "common_gotobehavior.h"
#include "common_guidebehavior.h"
#include "common_idlebehavior.h"
#include "common_itlbehavior.h"
#include "common_largeshipbehavior.h"
#include "common_launchbehavior.h"
#include "common_lootbehavior.h"
#include "common_nullbehavior.h"
#include "common_strafebehavior.h"
#include "common_trailbehavior.h"

#include <cstring>
#include <new>

// ============================================================================
// BehaviorTypes
// ============================================================================

// ----------------------------------------------------------------------------
// Behavior type registry — retail DAT_063fc540 / DAT_063fc8e0 / DAT_063fc8e4.
//
// Recovered from the twenty-one .CRT$XC static initialisers listed in the
// module init table at .data:063ea310..063ea360 (Common.dll.txt 208140-209320),
// one per behavior type. Each one is the same shape, e.g. the id-20 "Delay"
// initialiser at 062d15b0:
//
//     MOV  dword ptr [063fc540], .rdata:063a022c   ; +0x00 vptr
//     MOV  dword ptr [063fc544], 0x14              ; +0x04 type id
//     ... strlen(name) clamped to 0x1f, MOVSD/MOVSB copy of the literal ...
//     MOV  byte  ptr [063fc54c + len], 0           ; +0x0C name buffer, NUL at len
//     MOV  [063fc548], len                         ; +0x08 cached name length
//     MOV  dword ptr [063fc8e4 + 0x14*4], 063fc540 ; registry[id] = &descriptor
//     INC  [063fc8e0]                              ; ++count
//
// so the descriptor is exactly {vptr, int Id, int NameLength, char Name[32]} =
// 0x2C bytes, which is precisely the element stride of the 0x063fc540..0x063fc8dc
// block (twenty-one entries, laid out in DESCENDING id order: id 20 first at
// 0x063fc540, id 0 "NULL" last at 0x063fc8b0). DAT_063fc8e0 is the count and
// DAT_063fc8e4 the 21-entry `REP STOSD ECX=0x15`-cleared pointer array
// (zeroed by the initialiser at 062d0b90), running to 0x063fc938.
//
// Each descriptor's vtable is a three-slot .rdata block sitting 0x0C bytes ahead
// of its own name literal (e.g. 063a002c for "NULL" at 063a0038):
//   slot 0 (+0x00): get_type_id()   — ICF-folded onto TractorArm::GetMode, i.e.
//                                     the one-instruction `return *(this+4)`
//   slot 1 (+0x04): create_behavior() — the only per-type slot (FUN_062d16d0 for
//                                     NULL, FUN_062d1760 for Buzz, ... stride 0x70)
//   slot 2 (+0x08): get_name()      — ICF-folded onto LAB_062d1ae0, shared by all
//                                     21 vtables (`return this+0x0C`)
// which is exactly what get_behavior_name/get_behavior_id/CREATE_BEHAVIOR below
// call through (+0x08 and +0x04).
//
// CLOSED 2026-09-17 (claude:common-factory). The note that stood here said create_behavior()
// was "still open" and that every descriptor "inherits the base CreateBehavior() and returns
// nullptr for now". It does not any more: all twenty-one types construct.
//
// WHAT THE TWENTY-ONE FACTORY BODIES ACTUALLY DO, read off .text:062d16d0..062d1f90 (the
// twenty-one entries the descriptor vtables' +0x04 slots point at, one per id in ascending id
// order -- their starts are NOT evenly spaced, so they were taken from the .rdata slots and
// from the tracker's own function boundaries, not from an assumed stride):
//     PUSH <size>            ; CALL MSVCRT.DLL::operator_new
//     TEST EAX,EAX / JZ      ; allocation failure returns null
//     MOV  ECX,[ESI + 0x4]   ; ESI is the descriptor; +0x04 is its Id
//     PUSH ECX               ; ...which is the constructor's argument
//     MOV  ECX,EAX
//     CALL <that type's ctor>
// so a factory is exactly `new(nothrow) T(TypeId)`. The ids, retail sizes and constructors,
// in registry order:
//     0  NULL             0x2c   (base ctor FUN_062d2020 + vptr 063a0244 inline)
//     1  Buzz             0x354  FUN_062c5af0      11 Large Ship (move) 0x180 FUN_062cfe20
//     2  Goto             0x3fc  FUN_062fd620      12 Cruise            0x2c  FUN_062ce360
//     3  Trail            0x248  FUN_062ca350      13 Strafe            0x1bc FUN_062c9540
//     4  Flee             0xe8   FUN_062c7020      14 Guide             0x124 FUN_062cf230
//     5  Evade            0x2c0  FUN_062c7790      15 Face              0x1dc FUN_062c88e0
//     6  Idle             0x40   FUN_062cd530      16 Loot              0x180 FUN_062ce400
//     7  Dock             0x470  FUN_062c2e60      17 Follow            0x2bc FUN_062ccaf0
//     8  Launch           0x234  FUN_062cd640      18 DrasticEvade      0x44  FUN_062c8730
//     9  InstantTradeLane 0x5c   FUN_062cee50      19 FreeFlight        0x400 FUN_062c6d10
//    10  Formation        0x2b8  FUN_062cbfe0      20 Delay             0x48  FUN_062d0a10
// Every one of those constructors is now `explicit XBehavior(int InTypeId)` in this port, and
// every one of those sizes is pinned by a `static_assert(sizeof(X) <= ...)` in the class's own
// header. Id 8 deserves a note: several tracker items still describe that row as having an
// "empty-string name", because Ghidra found no string literal inline in the .rdata row at
// 063a00e8. The name is not inline -- the id-8 static initialiser at .text:062d0fba copies it
// from .rdata:0639a06c, and it is "Launch". The row is LaunchBehavior's.
//
// HOW THE PER-TYPE SLOT IS MODELLED. Retail has twenty-one descriptor vtables, identical
// except for slot +0x04. The port gets that with one template subclass per behavior type
// (TBehaviorTypeDesc<T> below): each instantiation is its own C++ type with its own vtable and
// its own create_behavior override, it adds no data member and no new virtual, so every
// descriptor is still exactly the {vptr, int Id, int NameLength, char Name[32]} = 0x2C object
// the initialisers build.
class BehaviorTypeDesc;

// DAT_063fc8e0 -- the number of registered descriptors; DAT_063fc8e4 -- registry[id] =
// &descriptor, a fixed 0x15-entry static. Retail's first initialiser 062d0b90 zeroes both
// (count = 0, REP STOSD ECX=0x15 over the array) before the twenty-one descriptor
// initialisers run; static zero-initialisation does the same here, ahead of every dynamic
// initialiser in this TU. Each descriptor then registers itself (BehaviorTypeDesc's ctor).
static int GBehaviorCount;
static BehaviorTypeDesc* GBehaviorRegistry[0x15];

class BehaviorTypeDesc
{
public:
    explicit BehaviorTypeDesc(int InTypeId, char const* InName)
        : TypeId(InTypeId), NameLength(0)
    {
        // 062d15b2..062d1604: strlen, clamp to 0x1f, copy, terminate at [len].
        int Length = static_cast<int>(strlen(InName));
        if (Length > 0x1F)
            Length = 0x1F;
        memcpy(Name, InName, static_cast<size_t>(Length));
        Name[Length] = '\0';
        NameLength = Length;
        // 062d1604..062d1621 (id 20; same tail in all twenty-one initialisers):
        // registry[id] = &descriptor, then ++count.
        GBehaviorRegistry[TypeId] = this;
        ++GBehaviorCount;
    }
    // No virtual destructor, deliberately: retail's vtable is exactly three
    // slots (063a002c..063a0034) with get_type_id first, and these descriptors
    // are file-scope statics that are never deleted through a base pointer.
    // Adding one would insert an MSVC scalar-deleting-dtor slot at index 0 and
    // shift get_name off the +0x08 offset the three callers below hand-index.

    virtual int get_type_id() const { return TypeId; }           // vtable +0x00
    // vtable +0x04. Only the twenty-one TBehaviorTypeDesc<T> instantiations below ever reach
    // the registry, so this base body is unreachable through it; it stays non-pure so the
    // class is still a plain constructible descriptor, and null is what retail's own
    // allocation-failure path returns.
    virtual Behavior* create_behavior() { return nullptr; }      // vtable +0x04
    virtual char const* get_name() const { return Name; }        // vtable +0x08

    int TypeId;         // +0x04
    int NameLength;     // +0x08
    char Name[32];      // +0x0C
};

// One C++ type per retail descriptor vtable. Adds no data and no new virtual, so
// sizeof(TBehaviorTypeDesc<T>) == sizeof(BehaviorTypeDesc) == 0x2C for every T.
//
// `new (std::nothrow)` rather than plain `new` is deliberate and is the faithful reading:
// retail calls MSVCRT's `operator new`, which in that CRT returns null on failure, and every
// factory body has an explicit `TEST EAX,EAX; JZ <return 0>` for exactly that case. Throwing
// here would turn a path retail handles into one it does not.
template <class BehaviorClass>
class TBehaviorTypeDesc : public BehaviorTypeDesc
{
public:
    TBehaviorTypeDesc(int InTypeId, char const* InName)
        : BehaviorTypeDesc(InTypeId, InName)
    {
    }

    Behavior* create_behavior() override
    {
        return new (std::nothrow) BehaviorClass(TypeId);
    }
};
static_assert(sizeof(TBehaviorTypeDesc<NullBehavior>) == sizeof(BehaviorTypeDesc),
              "a typed descriptor must stay the same 0x2C object retail's initialisers build");

// Retail id order, read straight off the twenty-one initialisers (the literal
// pushed as the id at +0x04 is given in the comment on each line; the class each one
// constructs is the ctor its own factory body calls -- see the table in the comment above).
static TBehaviorTypeDesc<NullBehavior>             GDescNull(0, "NULL");                      // 062d0bb0
static TBehaviorTypeDesc<BuzzBehavior>             GDescBuzz(1, "Buzz");                      // 062d0c30
static TBehaviorTypeDesc<GotoBehavior>             GDescGoto(2, "Goto");                      // 062d0cb0
static TBehaviorTypeDesc<TrailBehavior>            GDescTrail(3, "Trail");                    // 062d0d30
static TBehaviorTypeDesc<FleeBehavior>             GDescFlee(4, "Flee");                      // 062d0db0
static TBehaviorTypeDesc<EvadeBehavior>            GDescEvade(5, "Evade");                    // 062d0e30
static TBehaviorTypeDesc<IdleBehavior>             GDescIdle(6, "Idle");                      // 062d0eb0
static TBehaviorTypeDesc<DockBehavior>             GDescDock(7, "Dock");                      // 062d0f30
static TBehaviorTypeDesc<LaunchBehavior>           GDescLaunch(8, "Launch");                  // 062d0fb0
static TBehaviorTypeDesc<InstantTradeLaneBehavior> GDescItl(9, "InstantTradeLane");           // 062d1030
static TBehaviorTypeDesc<FormationBehavior>        GDescFormation(10, "Formation");           // 062d10b0
static TBehaviorTypeDesc<LargeShipBehavior>        GDescLargeShip(11, "Large Ship (move)");   // 062d1130
static TBehaviorTypeDesc<CruiseBehavior>           GDescCruise(12, "Cruise");                 // 062d11b0
static TBehaviorTypeDesc<StrafeBehavior>           GDescStrafe(13, "Strafe");                 // 062d1230
static TBehaviorTypeDesc<GuideBehavior>            GDescGuide(14, "Guide");                   // 062d12b0
static TBehaviorTypeDesc<FaceBehavior>             GDescFace(15, "Face");                     // 062d1330
static TBehaviorTypeDesc<LootBehavior>             GDescLoot(16, "Loot");                     // 062d13b0
static TBehaviorTypeDesc<FollowBehavior>           GDescFollow(17, "Follow");                 // 062d1430
static TBehaviorTypeDesc<DrasticEvadeBehavior>     GDescDrasticEvade(18, "DrasticEvade");     // 062d14b0
static TBehaviorTypeDesc<FreeFlightBehavior>       GDescFreeFlight(19, "FreeFlight");         // 062d1530
static TBehaviorTypeDesc<DelayBehavior>            GDescDelay(20, "Delay");                   // 062d15b0

// DACOM.DLL!stricmp (Common IAT 0639902c), common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

// The three lookups below index the registry by id with no range test, as retail does: every
// id that reaches them is 0..20 (BehaviorManager's CREATE_BEHAVIOR loop) or -1 (handled by
// get_behavior_name). Retail's array is a fixed 0x15-dword static followed by a float at
// 063fc938, so an out-of-range id there reads a junk pointer; the port has the same hole.

int __cdecl BehaviorTypes::get_behavior_id(char const* Name)
{
    // .text:062d1680..062d16cd: linear search, the count re-read from DAT_063fc8e0 on every
    // pass; get_name() (+0x08) of each entry, no null test on the entry; DACOM stricmp with
    // the entry's name first.
    for (int I = 0; I < GBehaviorCount; ++I)
    {
        if (CallDacomStricmp(GBehaviorRegistry[I]->get_name(), Name) == 0)
            return I;
    }
    return -1;
}

char const* __cdecl BehaviorTypes::get_behavior_name(int Id)
{
    // .text:062d1650..062d1671. Id -1 returns the string DAT_063ec08c points at.
    if (Id == -1)
        return "BEHAVIOR_ID_NONE";
    BehaviorTypeDesc* Desc = GBehaviorRegistry[Id];
    if (Desc == nullptr)
        return nullptr;
    return Desc->get_name();                                   // JMP [vt+0x08]
}

Behavior* __cdecl CREATE_BEHAVIOR(int param_1)
{
    // .text:062d1630..062d1646: registry[id]; null -> 0, else JMP [vt+0x04] create_behavior().
    BehaviorTypeDesc* Desc = GBehaviorRegistry[param_1];
    if (Desc == nullptr)
        return nullptr;
    return Desc->create_behavior();
}
