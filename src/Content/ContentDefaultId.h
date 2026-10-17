// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_id.h"

// Content.dll "default" static ID. A second header, included after ContentStaticIds.h by the
// retail translation units that carry one more _initterm thunk writing CreateID("default")
// into a TU-private global (among them TU0-14 06ea1200..06ea58c0, TU26 06eeec30, TU27
// 06eef390, TU33 06efeae0, TU35 06f041a0, TU38 06f0e200). Internal linkage, so each including
// source file gets its own copy, as in retail.

static unsigned int const GIdDefault = CreateID("default");
