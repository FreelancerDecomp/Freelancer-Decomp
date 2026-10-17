// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_id.h"

// Content.dll per-TU static IDs.
//
// Every Content translation unit in retail starts with the same 16 dynamic
// initializers (_initterm thunks), e.g. TU51 06f57cb0..06f57e90 writing
// DAT_06fd0e2c..DAT_06fd0df0, TU0 06ea1000..06ea11e0 writing
// DAT_06fce960..DAT_06fce924: CreateID(<literal>) stored into a TU-private
// global. That is a header with internal-linkage globals included by every
// source file. This header reproduces it: each including TU gets its own copy,
// initialized in this declaration order, exactly like retail's per-TU thunks.
// ("default", present only in 26 of the TUs, comes from a different header and
// is not part of this set.)

static unsigned int const GIdAny = CreateID("any");
static unsigned int const GIdHouseFighters = CreateID("house_fighters");
static unsigned int const GIdFighters = CreateID("fighters");
static unsigned int const GIdFreighters = CreateID("freighters");
static unsigned int const GIdSmallShips = CreateID("small_ships");
static unsigned int const GIdCapitals = CreateID("capitals");
static unsigned int const GIdTransports = CreateID("transports");
static unsigned int const GIdMerchants = CreateID("merchants");
static unsigned int const GIdUtilityShips = CreateID("utility_ships");
static unsigned int const GIdLargeShips = CreateID("large_ships");
static unsigned int const GIdFighter = CreateID("fighter");
static unsigned int const GIdFreighter = CreateID("freighter");
static unsigned int const GIdCanUseBerths = CreateID("can_use_berths");
static unsigned int const GIdCanUseSmallMoors = CreateID("can_use_small_moors");
static unsigned int const GIdCanUseMediumMoors = CreateID("can_use_medium_moors");
static unsigned int const GIdCanUseLargeMoors = CreateID("can_use_large_moors");
