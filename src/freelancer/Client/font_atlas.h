// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FontString GPU atlas — retail Font.cpp FUN_00412670 / FUN_00412cc0 / FUN_004127d0.
// Shared 256×256 GDI scratch → ExtTextOutW → GetDIBits → Pipeline Lock → A1R5G5B5 pages.
// Do NOT substitute RichTextPane / DeviceTRC / per-string A8R8G8B8 scaffolds.

#include <windows.h>

class IFont;

// Atlas page — retail operator_new(0x28) from FUN_00412cc0.
struct FFontAtlasPage
{
    void* Texture;          // +0x00 Pipeline texture handle
    unsigned PageW;         // +0x04 power-of-two
    unsigned PageH;         // +0x08 power-of-two
    unsigned ContentH;      // +0x0c measured string height (cache key)
    int SrcX;               // +0x10 offset into string bitmap
    int SrcY;               // +0x14
    unsigned UsedW;         // +0x18 tile used width
    unsigned UsedH;         // +0x1c tile used height
    FFontAtlasPage* NextX;  // +0x20 horizontal sibling
    FFontAtlasPage* NextY;  // +0x24 next row
};

static_assert(sizeof(FFontAtlasPage) == 0x28, "atlas page 0x28");

// FUN_00412670 — lazy shared HDC / 256×256 DIB scratch (DAT_00616654..6c).
void FontAtlasEnsureScratch();

// FUN_004180c0 — font device reset from the render-manager init: bump G_FontLayoutGeneration,
// drop the number-face glyphs, delete and rebuild the GDI scratch (FUN_00412670).
void FontDeviceReset();

// The GDI scratch teardown inlined into FUN_004180c0 (00418119..0041815a) and FUN_004174a0
// (004174a0..004174e6): free the DIB block (DAT_00616660, then zeroed), select the previous
// bitmap back (DAT_00616658), delete the bitmap (DAT_00616654) and the DC (DAT_00616664).
void FontAtlasReleaseScratch();

// FUN_004174a0 00417545..004175c0: destroy every page of the free-page pool DAT_00616644..48 --
// while it is not empty, take the first page, DestroyTexture (Pipeline +0xb8) and subtract
// PageW * PageH * 2 from DAT_00616650 when DALib::Pipeline is set, free the page, erase the slot.
void FontAtlasReleasePagePool();

// Release glyph-grid pages back to DAT_00616648 free-list (keep GPU textures).
void FontAtlasFreePages(FFontAtlasPage*& Head);

// FUN_004127d0 (thiscall on a FontString, RET 4) — size through the string's GetSize (vt+0x18),
// build or reuse its page grid (+0x14), ExtTextOutW each tile into the GDI scratch and upload
// it as A1R5G5B5. bSkipSizeCheck == 0 runs the power-of-two / scratch-size check at
// 0041281f..0041288c (a failure sets DAT_00616698 and returns 0) and builds the grid from the
// rounded extents; non-zero skips both. The pixel upload does not depend on it (00412a03 reuses
// the slot as the success flag). The NumberString face callers pass 0 (0041385b, 0041389f,
// 00414125, 004141ad, 00415134); FUN_004152e0 passes its own argument (004154a5).
// Returns 1 when the grid is current or every tile was drawn, 0 otherwise.
class FontStringChild;
unsigned char FontAtlasRasterize(FontStringChild* This, unsigned char bSkipSizeCheck);

// Material key for ScreenQuadSetMaterialKey during FontString paint (DAT_00616670).
unsigned FontAtlasMaterialKey();
void FontAtlasSetMaterialKey(unsigned Key);

// DAT_00616674 — packed ARGB tint written by FUN_00417780 before FontString draw.
unsigned FontAtlasDrawColor();
void FontAtlasSetDrawColor(unsigned PackedArgb);
