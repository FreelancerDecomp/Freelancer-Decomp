// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Decimal MSVCRT atof semantics for Common's INI paths; implemented with integer
// mantissa arithmetic, without loading an old CRT or inheriting x87 precision.
double MsvcrtAtof(char const* Text);
