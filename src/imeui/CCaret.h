// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Redirect -- CCaret is declared once, in src/include/imeui.h (cross-DLL rule: freelancer.exe
// derives from IMEInputBox, which holds a CCaret by value).
#include "imeui.h"
