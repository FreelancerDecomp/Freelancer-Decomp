// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Small idioms the Intro.cpp pages (PageServerSelect, PageCharacterSelect) spell out inline in
// retail. None is a retail function of its own except where a FUN_ address is named.

#include "common_text.h" // RenderDisplayList, RDLNode, TextRenderAttributes

#include <vector>

struct Win3D;

namespace IntroPageUtil
{
    // The idiom every RDL append spells out: the node cloned (slot +0x1c), the clone pushed by
    // FUN_0046c800 / FUN_0046bb00 (whose cell clones it again, FUN_0046ea00), the first clone
    // deleted (FUN_0045eeb0 / the inline slot-0 call).
    void AppendNodeClone(RenderDisplayList& Rdl, RDLNode const& Node);

    // The list emptied (FUN_0046c860 over begin..end), then the IDS text loaded (FUN_0057da40).
    void LoadIds(RenderDisplayList& Rdl, unsigned Ids);

    // The same, the trailing paragraph cut when the load worked (FUN_0057eb40).
    void LoadIdsCut(RenderDisplayList& Rdl, unsigned Ids);

    // The same behind a StyleNode.
    void LoadStyledIds(RenderDisplayList& Rdl, unsigned short Style, unsigned Ids);

    // FUN_00571220 (RET 0x14) -- TextRenderAttributes' bit fields: +0x00 bit 0 = A, bit 1 = B,
    // bit 2 = C & 1, bits 3..7 = D; the low 24 bits of the dword at +0x01 = Rgb (the struct's
    // fifth byte is kept). The port's attribute bundle is the first dword.
    TextRenderAttributes PackTextAttributes(unsigned char A, unsigned char B, unsigned char C,
                                            unsigned char D, unsigned long Rgb);

    // The inline `AND [obj+0x6c],0xfc` / `OR [obj+0x6c],3` pair.
    void HideWin3D(Win3D* Object);
    void ShowWin3D(Win3D* Object);

    // &DAT_00679bb4, the HUD chrome colour.
    unsigned long const* ChromeColor();

    // FUN_005ad970 -- a VC6 vector<float>'s size ((last - first) >> 2, 0 for a null first).
    int EdgeCount(std::vector<float> const& Edges);
}
