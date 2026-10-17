// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// RNG stream and the Lucas ladder from movie.dll.
// The retail address is on each definition.

// 0x06acca95. RET 0x10.
// Seed and SeedLength are used together: either pointer null skips both.
// *Seed is the seed-buffer address. *SeedLength is its byte count and is stored back.
void __stdcall GenerateRngBytes(unsigned int* Seed, unsigned int* SeedLength, unsigned char* Output, unsigned int Length);

// 0x06acd2b0. RET 0x1C.
// First is P, Second is Q, Third is the exponent. Count is only the exponent's limb count
// for the bit-length call. Fourth and Fifth are the two running values.
// The limb width of those copies is Context[0x40], and Context[0x451] is the starting Fourth value.
void __stdcall LadderLucasModulo(unsigned int* First, unsigned int* Second, unsigned int* Third, unsigned int Count, unsigned int* Fourth, unsigned int* Fifth, unsigned int* Context);
