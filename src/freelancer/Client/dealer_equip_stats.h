// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FUN_00484090 — Equipment/Ship Dealer item-stat / tip text builder.
// Full branch table: docs/re/dealer_equip_stats.md

#include "common_text.h"

// OutRdl receives one AppendTextMaybeXmlRdl (FUN_0057e2c0) per finished line.
// bEmitLabels / bEmitValues match retail param_3 / param_4 (caller splits label vs value columns).
void BuildDealerEquipmentStatText(RenderDisplayList& OutRdl, unsigned ArchId, bool bEmitLabels,
                                  bool bEmitValues);
