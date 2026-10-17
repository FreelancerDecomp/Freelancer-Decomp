// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

/* Retail EXE/zlib.dll (Gilles Vollant, 2002-03-13) returns "1.1.4".
 * DALib hardcodes that string into deflateInit_/inflateInit_ and does not
 * import zlibVersion, but GetProcAddress("zlibVersion") should match. */

const char* __stdcall FreelancerZlibVersion(void)
{
    return "1.1.4";
}
