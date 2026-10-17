// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Point hashing shared by the IVP surface builders (ledge soup weld path,
// pointsoup / polygon-convex chain). Bodies live in
// ivp_surface_builder_ledge_soup.cpp.

// Retail FUN_0636cfa0 (.text:0636cfa0-0636cfcf, stdcall, RET 4): CRC-32 of
// the first 12 bytes (x, y, z) of a point with the reflected table
// .data:DAT_063edcdc (polynomial 0xEDB88320), seed 0xffffffff, no final
// inversion, then OR 0x80000000 (the IVP_VHash "occupied" bit).
// Callers: 0636b450, 0636b4c3, 0636c312, 0636c32b, 0636c652, 0636c6a5,
// 0636dbda, 0636dc31, 0637a3ee, 0637a416.
unsigned IVP_PointHashIndex(const void* Point);

// Retail FUN_0636cfe0: REPE CMPSD over 3 dwords; SETZ -> 1 when x, y and z
// match bit for bit, else 0 (stdcall, RET 8). Slot 0 (compare) of the point
// hash vtables .rdata:063a58f8 (ledge soup), 063a590c and 063a5918.
int IVP_PointHashCompare(const void* A, const void* B);
