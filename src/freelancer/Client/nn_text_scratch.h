// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail's two shared wide-text scratch buffers and their capacity global. Many text builders
// fill one of them with an IDS string (FUN_004347e0 with the capacity DAT_006119f8), format or
// concatenate into it, and hand it to a control or a tooltip record in the same call:
// NN_Inventory's SetProperty 0x4f (0x00479fec..0x0047a046), the dealer's stat text
// FUN_00484090, the HUD status / target panels, NN_MainButton (0x0045af16..) and the rollover
// (0x004ba08b..). Nothing keeps a pointer into them across calls.
//
//   DAT_0066dc60  .bss  0x1000 wchar_t (ends at 0x0066fc60)
//   DAT_0066fc60  .bss  0x1000 wchar_t (ends at 0x00671c60)
//   DAT_006119f8  .data int = 0x1000, the count every caller passes for either buffer

extern wchar_t G_NnTextScratchA[0x1000]; // DAT_0066dc60
extern wchar_t G_NnTextScratchB[0x1000]; // DAT_0066fc60
extern int G_NnTextScratchCapacity;      // DAT_006119f8
