// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal declarations shared by the RichDisplayList translation units:
// common_textrender.cpp, common_rdlnodes.cpp, common_renderdisplaylist.cpp and common_rdlio.cpp.
// Retail keeps them in one TU (E:\FL\Scratch\Source\Common\RichDisplayList.cpp); none of these is
// exported. Split out of common_text.cpp (CMN-SPLIT phase B, 2026-10-14).

#include "common_text.h"

namespace CmnText
{
    // DAT_0640957c — global default text attributes (packed style-index/color, same layout as
    // TextRenderAttributes::Packed: byte0=style index, bytes1-3=color). Retail seeds this once at
    // DLL load via a static POD initializer (0xc6290, item 2382 -- confirmed pure compiler-generated
    // glue, no port action needed there) to {byte0=0, bytes1-3=0xFFFFFF} read little-endian, i.e.
    // 0xFFFFFF00 (matching the already-identified GDefaultTRANodeAttribMask constant TRANode's own
    // ctor uses, both being the SAME retail global -- consolidated here so both consumers share
    // one definition instead of risking two independently-guessed values).
    // Every TextRenderContext-family ctor (TextRenderContext itself, both DeviceTRC ctors, TRANode
    // -- see 0xc7380/0xc7b40/0xc7bf0/0xc8658) copies its CURRENT value into its own attrs field(s)
    // at construction.
    static const unsigned long GDefaultTextAttribsPacked = 0xFFFFFF00u;

    // DAT_063ed66c, the rich-text scale InitializeRichText / ReinitializeRichTextFonts set.
    // Defined in common_textrender.cpp; DeviceTRC::execute_image and ImageNode::GetVisualSize
    // (common_rdlnodes.cpp) read it.
    extern float GRichTextScaleY;

    // FUN_0632c490: links a new cell before Where and stores Value->Clone() in it. Defined in
    // common_renderdisplaylist.cpp.
    void InsertRdlCloneBefore(RenderDisplayList& Rdl, FRdlListNode* Where, RDLNode* const& Value);

    // The append sequence of every RDLX tag and binary RDL record: Node.Clone(), then
    // InsertRdlCloneBefore at the sentinel (which clones that clone), then the first clone is
    // deleted. Defined with the RDLX parser in common_rdlio.cpp.
    void RdlxAppendNode(RenderDisplayList& Rdl, RDLNode const& Node);
}

using namespace CmnText;
