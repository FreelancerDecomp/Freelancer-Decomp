// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// SHA-1 from movie.dll. Retail addresses are on the definitions.
// The context object is 0x74 bytes. State is five words. The compressors are
// __stdcall, RET 8, and take the state plus sixteen block words.

// Bytes +0x00..+0x17 are part of the object. Init, update, and final do not
// read or write them.
struct FSha1Context
{
    unsigned char Field_0x00[0x18];
    unsigned int State[5];
    unsigned int ByteCountHigh;
    unsigned int ByteCountLow;
    unsigned char Block[64];
};

// Byte-swaps each of the 16 block words, expands them to 80, folds the block into State.
void __stdcall Sha1CompressSwapped(unsigned int* State, const unsigned int* Block);

// Copies the 16 block words unchanged, then the same expansion and fold.
void __stdcall Sha1CompressUnswapped(unsigned int* State, const unsigned int* Block);

// Clears the byte count and loads the SHA-1 IV.
void __stdcall Sha1Init(FSha1Context* Context);

// Absorbs Length bytes. Completes a partial block, compresses whole blocks, stores the tail.
void __stdcall Sha1UpdateSwapped(FSha1Context* Context, const unsigned char* Data, unsigned int Length);

// Same absorption. The compressor copies block words unchanged.
void __stdcall Sha1UpdateUnswapped(FSha1Context* Context, const unsigned char* Data, unsigned int Length);

// Pads with 0x80, zeros, and the 64-bit bit length, absorbs that through the swapped
// update, copies the five state words, then inits the context again.
void __stdcall Sha1Final(FSha1Context* Context, unsigned int* Digest);
