// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy.h"

// Private client forwarding boundary; the value stays in its original float object.
void ForwardAlchemyEffectFloat(IFxEffect* Effect,
                              GENRESULT (DACOM_API IFxEffect::*Method)(float),
                              const float& Value);
