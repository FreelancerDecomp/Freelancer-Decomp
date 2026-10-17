// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The server's message of the day (retail .data:0042787c, a std::wstring built empty by the
// static initializer .text:0040ab90 and destroyed by its atexit thunk .text:0040ac30).
#pragma once

#include <string>

extern std::wstring GMotd;

// .text:0040ac80: writes GMotd as raw UTF-16 (no BOM, no terminator) to
// <local user dir>\motd.dat through PlayerDB::MakeLocalUserPath.
void SaveMotd();
