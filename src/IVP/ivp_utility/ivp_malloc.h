// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// IVP's aligned block allocator pair (retail 0x06343fc0 / 0x06343ff0), defined
// in ivp_malloc.cpp (moved from src/Common/common_stubs.cpp, 2026-10-14). The
// plain malloc / free / calloc thunks next to them (0x06343f70 / 0x06343fb0 /
// 0x06343f80) are the CRT functions, called directly at every port site.
//
// Users: compact ledges and surfaces (src/IVP/ivp_compact_builder, the
// surface managers) and PhyArch / PhySys in src/Common.
// ============================================================================

// 0x06343fc0 (formerly extern "C" FUN_06343fc0): mallocs `size + align + 8`
// bytes, rounds up to `align` (a power of two at every call site), writes
// the 0x65981234 marker at the raw block start and keeps the raw pointer at
// result[-1]. The block is not zeroed.
void* __cdecl ivp_malloc_aligned(unsigned Size, unsigned Align);

// 0x06343ff0 (formerly extern "C" FUN_06343ff0): frees the raw pointer kept
// at alignedPtr[-1]. No null check -- every caller tests first.
void __cdecl ivp_free_aligned(void* AlignedPtr);
