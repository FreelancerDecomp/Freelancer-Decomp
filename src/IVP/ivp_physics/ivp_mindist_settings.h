// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================================
// IVP_Mindist_Settings -- retail's one global mindist/friction tolerance object
// at .data:0658aa10 (0x138 bytes, .bss: no file-backed contents).
//
// Built once at DLL load: the static-initializer table entry .data:063ea60c
// points at LAB_0634da30 (`JMP LAB_0634da40`), and LAB_0634da40 is
// `MOV ECX,0x0658aa10; JMP FUN_0634db40` -- the constructor below runs on the
// global's address. The constructor (FUN_0634db40) calls the tolerance setter
// FUN_0634da50 with 0.01f (0x3c23d70a) and the setter FUN_0634db30 with 1.0f,
// then stores 1000 at +0x12c. FUN_0634da50 and FUN_0634db30 have no other
// caller in the listing.
//
// Until 2026-10-08 the port had three separate stand-ins for this one object:
// two lazily-built float arrays (ivp_mindist_limiter.cpp, ivp_ledge_sweep_solver.cpp)
// filled by an extern "C" copy of FUN_0634db40 in common_stubs.cpp, and a
// placeholder `g_UnresolvedSimClock_DAT_0658ab1c = 0.0f` for the +0x10c field
// (which this object's constructor actually sets to 0.02f). All readers now go
// through G_IVP_MindistSettings.
//
// Readers by global address (offset = address - 0x0658aa10):
//   DAT_0658aa10 +0x000   DAT_0658aa18 +0x008 (ramp base)   DAT_0658ab1c +0x10c
//   DAT_0658ab24 +0x114   DAT_0658ab28 +0x118   DAT_0658ab2c +0x11c
//   DAT_0658ab30 +0x120   DAT_0658ab34 +0x124   DAT_0658ab38 +0x128
//   DAT_0658ab3c +0x12c   DAT_0658ab40 +0x130   DAT_0658ab44 +0x134
// Retail strings name none of the fields. Names follow the IVP SDK's
// set_collision_tolerance, whose formulas FUN_0634da50 matches term for term;
// +0x130 and +0x134 have no SDK counterpart we can prove and stay named by offset.
// ============================================================================
class IVP_Mindist_Settings
{
public:
    // Names below follow Ipion's IVP_Mindist_Settings::set_collision_tolerance, whose
    // formulas FUN_0634da50 reproduces term for term (formerly Field_0xNNN).
    float real_coll_dist;      // +0x000 = D * 0.1f (formerly Field_0x000)
    float min_coll_dists;      // +0x004 = D * 0.9f + D * 0.1f (formerly Field_0x004)
    float CollDistRamp[0x40];  // +0x008..+0x104: [k] = k * (1/64) * (max_coll_dist - min_coll_dists) + min_coll_dists
                               // Readers index it with `(StateFlags >> 22) & 0xff`, i.e. past its end
                               // into the fields below -- see G_IVP_MindistSettingsStorage.
    float max_coll_dist;       // +0x108 = min_coll_dists (same x87 value, stored twice) (formerly Field_0x108)
    // +0x10c (formerly Field_0x10C): the gap a friction contact is driven to --
    // the friction solver's desired distance (06368ad9-06368b08:
    // Desired = Factor * (this - Gap) + RelSpeed) and every new contact
    // point's starting gap (:06353b22, IVP_Contact_Point ctor); also the
    // single-friction normal-impulse target (:06356568).
    float friction_dist;       // +0x10c = D + max_coll_dist        (DAT_0658ab1c)
    // +0x110: the gap a fresh contact point is parked at (:IVP_Contact_Point ctor path,
    // ivp_contact_point.cpp) (formerly Field_0x110).
    float keeper_dist;         // +0x110 = D * 0.3f + friction_dist
    // +0x114: sqrt(2 * 9.81 * (keeper_dist - min_coll_dists)), the speed of a fall from
    // keeper_dist (formerly Field_0x114).
    float speed_after_keeper_dist; // +0x114                        (DAT_0658ab24)
    // +0x118 (formerly Field_0x118): the slack above friction_dist; a kept
    // friction contact whose gap exceeds friction_dist + this is moved to the
    // head of its list (06368dc0-06368dd6).
    float distance_keepers_safety; // +0x118 = D * 0.01f             (DAT_0658ab28)
    // +0x11c (formerly Field_0x11C): the gap at or above which a friction
    // contact leaves the friction system (:06368d95, :06356585). Other
    // readers: :0635646a, :0634f2b9.
    float max_distance_for_friction; // +0x11c = D * 2.5f + friction_dist (DAT_0658ab2c)
    // +0x120: a contact whose gap exceeds this leaves the impact system (ivp_contact_point.cpp)
    // (formerly Field_0x120).
    float max_distance_for_impact_system; // +0x120 = D * 20.0f + friction_dist (DAT_0658ab30)
    float mindist_change_force_dist;      // +0x124 = min_coll_dists * 0.1f (DAT_0658ab34) (formerly Field_0x124)
    // +0x128: the base of the impact solver's rescue speed (ivp_impact_solver.cpp)
    // (formerly Field_0x128).
    float min_vertical_speed_at_collision; // +0x128 = D + D        (DAT_0658ab38)
    // +0x12c: cap on spawned mindists, compared against get_spawned_mindist_count before
    // each spawn (06362... sites in ivp_mindist_limiter.cpp); PhySys overwrites it with
    // MAX_SPAWNED_MINDIST_COUNT (formerly Field_0x12C).
    int32_t max_spawned_mindist_count; // +0x12c = 1000 (constructor) (DAT_0658ab3c)
    float Field_0x130;         // +0x130 = 1.0f (FUN_0634db30)       (DAT_0658ab40)
    int32_t Field_0x134;       // +0x134 not written by the constructor; a counter of
                               // FUN_0634f320 (DAT_0658ab44). Zero in .bss.

    // FUN_0634db40 (.text:0634db40-0634db66).
    IVP_Mindist_Settings();

    // FUN_0634da50 (.text:0634da50-0634db2d, thiscall, RET 4).
    void SetCollisionTolerance(float D);

    // FUN_0634db30 (.text:0634db30-0634db3a, thiscall, RET 4): *(this+0x130) = Value.
    void SetField_0x130(float Value);
};

static_assert(offsetof(IVP_Mindist_Settings, CollDistRamp) == 0x008, "IVP_Mindist_Settings+0x008");
static_assert(offsetof(IVP_Mindist_Settings, max_coll_dist) == 0x108, "IVP_Mindist_Settings+0x108");
static_assert(offsetof(IVP_Mindist_Settings, friction_dist) == 0x10C, "IVP_Mindist_Settings+0x10c");
static_assert(offsetof(IVP_Mindist_Settings, distance_keepers_safety) == 0x118, "IVP_Mindist_Settings+0x118");
static_assert(offsetof(IVP_Mindist_Settings, max_distance_for_friction) == 0x11C, "IVP_Mindist_Settings+0x11c");
static_assert(offsetof(IVP_Mindist_Settings, max_spawned_mindist_count) == 0x12C, "IVP_Mindist_Settings+0x12c");
static_assert(offsetof(IVP_Mindist_Settings, Field_0x134) == 0x134, "IVP_Mindist_Settings+0x134");
static_assert(sizeof(IVP_Mindist_Settings) == 0x138, "IVP_Mindist_Settings is 0x138 bytes");

// The ramp index is `(StateFlags >> 22) & 0xff` (Common.dll.txt:0634f3bc-0634f3c9,
// 0634fe77-0634fe7f), so retail can address CollDistRamp[0xff], i.e. byte
// 0x008 + 0x3fc = 0x404 past the object -- far beyond the 0x138 bytes it owns, into
// whatever .data follows. The storage below keeps the in-range reads exact and
// makes the out-of-range ones read zero (retail would read the neighbouring
// globals). Open TODO for whoever proves the index never exceeds 0x3f.
struct FIVP_MindistSettingsStorage
{
    IVP_Mindist_Settings Settings;
    float Tail[0x102 - 0x4e];
};

extern FIVP_MindistSettingsStorage G_IVP_MindistSettingsStorage;

// The global object itself (retail .data:0658aa10).
extern IVP_Mindist_Settings& G_IVP_MindistSettings;

// DAT_0658aa18[Index] -- the ramp read with retail's unclamped index.
inline float IVP_MindistSettingsRamp(uint32_t Index)
{
    return (&G_IVP_MindistSettingsStorage.Settings.CollDistRamp[0])[Index];
}
