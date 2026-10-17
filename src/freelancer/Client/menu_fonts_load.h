// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FUN_0057e310 (rich_fonts.ini). fonts.ini (FUN_00417800) is font_imp.h LoadFonts; the
// TrueType fonts it builds are found by nickname with FindMenuFontImp (FUN_004177b0).

struct IFileSystem;

// FUN_0057e310, cdecl(value, filesystem): GetFileSysPath(path, RelPath, Filesys) first
// (0x0057e341..0x0057e34b). FUN_005b0c30 passes DALib::Data. [TrueType] "font" lines go to
// COMMON SetStandardFont, each [Style] becomes a RenderDisplayList handed to
// StyleCollection::set_style under the CRC-16 of its "name".
void LoadRichFontsFromIni(char const* RelPath, IFileSystem* Filesys);
