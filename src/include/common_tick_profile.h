// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include <stdint.h>

// Shared switch for the client and Common/IVP. Disabled unless explicitly enabled.
#ifndef FREELANCER_ENABLE_TICK_PROFILE
#define FREELANCER_ENABLE_TICK_PROFILE 0
#endif

// Port-only physics diagnostics requested by Giperion (CLIENT-BETA-LABEL).
// QPC ticks, inclusive and summed across all substeps of one client physics block.
enum class EPhysicsTickSection
{
    Simulate,
    PsiPrepare,
    SimUnits,
    CommitHulls,
    Contacts,
    SyncObjects,
    Count
};

struct FPhysicsTickProfile
{
    int64_t Ticks[static_cast<int>(EPhysicsTickSection::Count)] = {};
};

namespace PhySys
{
    // Diagnostic extension to the port's export surface, absent in retail.
    // Borrowed on this thread only; caller restores the previous sink before destruction.
    COMMON_API FPhysicsTickProfile* __cdecl SetTickProfile(FPhysicsTickProfile* Profile);
}
