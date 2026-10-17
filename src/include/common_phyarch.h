// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll PhyArch::LoadSurfaces — listing RVA 0x1fc80 (map 0001:0001EC80).
// Called from Root::load after mesh bind, skip .sph.

#include "common_ini.h"

#include <map>
#include <vector>

// The library's compact surface (the 'surf' chunk of a .sur file, read verbatim). Only the
// pointer is handled on the PhySys side.
class IVP_Compact_Surface;

namespace PhyArch
{
    // Retail Part (0x30 bytes): the mapped value of Archetype::Root::PhyParts (+0x48); retail
    // get_phy_part returns map node+0x10. Every special member below is a retail export.
    struct COMMON_API Part
    {
        // .text:0627f9b0 (??0Part@PhyArch@@QAE@XZ): byte +0x00 = 1, +0x04 = 0, empty hardpoint
        // vector. The extents are not written (in retail nor in the port).
        Part();
        // .text:0626c600 (??0Part@PhyArch@@QAE@ABU01@@Z): member copy -- the surface POINTER is
        // copied, not the surface (retail only copy-constructs from parts without one).
        Part(Part const& Other);
        // .text:0627f9d0 (??1Part@PhyArch@@QAE@XZ): frees the surface (FUN_06343ff0) and the
        // hardpoint vector.
        ~Part();
        // .text:0627fa90 (??4Part@PhyArch@@QAEAAU01@ABU01@@Z): frees the own surface, copies the
        // byte / extents / hardpoints and deep-copies the source surface.
        Part& operator=(Part const& Other);
        // .text:0627fa30 (?clear@Part@PhyArch@@QAEXXZ). Inlined by LoadSurfaces for a duplicate
        // part id (.text:0627ff6c-0627ff9b).
        void clear();

        // +0x00: 1 from the ctor and clear(), 0 from a '!fxd' chunk (.text:06280095).
        unsigned char bNotFixed;
        // +0x04: the 'surf' chunk -- an IVP compact surface read into a 16-byte aligned block
        // (FUN_06343fc0(size, 0x10)); its byte size is the dword at +0x1c shifted right by 8.
        IVP_Compact_Surface* Surface;
        Vector ExtentsMin;                  // +0x08 'exts'
        Vector ExtentsMax;                  // +0x14 'exts'
        std::vector<unsigned> HardpointIds; // +0x20 'hpid' (retail: allocator byte, vector at +0x24)
    };

    // Retail ?LoadSurfaces@PhyArch@@YA_NPBDAAV?$map@IUPart@PhyArch@@...@std@@@Z: fills the
    // caller's map, which is Archetype::Root::PhyParts (+0x48). Modern std::map, same shape.
    COMMON_API bool __cdecl LoadSurfaces(char const* SurPath, std::map<unsigned int, Part>& Parts);

    // 0x202d0: lazy-initialized default Part with unit box extents (-0.5..+0.5).
    COMMON_API Part const* __cdecl GetDefaultPart();

}
