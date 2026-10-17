// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FUN_0635a9e0 (Common.dll.txt:443970-443984, decomp-tracker item 3304,
// ported by D102 2026-09-09 while surveying the D84-flagged unprocessed range
// 0x06352000-0x0635f000). A tiny multiplicative-congruential PRNG: a single
// 32-bit global seed (retail DAT_063ee4dc) is multiplied by 75 (= 3*5*5, via
// three LEA-based multiplies -- Common.dll.txt:443974-443978) every call, the
// low 16 bits of the new seed are taken as the pseudo-random value, then
// scaled to a float via `float(x) * (1.0f/65536.0f)` (retail DAT_063a567c ==
// 0x37800000 == 2^-16, confirmed by reading its raw bytes) -- i.e. a float in
// [0, 1). No modulus beyond 32-bit register wraparound (retail relies on
// natural overflow, not an explicit `% M`).
//
// Confirmed live caller context (Common.dll.txt:06349855/0x063498bf, outside
// this pass's assigned address range): IVP_Real_Object's per-contact
// friction/anomaly "cooldown" timer reset -- `word[core+0x118] = 15 -
// (int)(IVP_FastRandomFloat() * K)` once the previous cooldown counts down to
// 0 -- i.e. this feeds a randomized re-arm delay for a contact-pair anomaly
// limiter, not gameplay-visible randomness. Kept here as a small, reusable,
// self-contained utility rather than inlined into that (unported) caller.
float IVP_FastRandomFloat();
