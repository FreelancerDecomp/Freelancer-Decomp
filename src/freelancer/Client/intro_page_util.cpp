// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "intro_page_util.h"

#include "ids_rdl.h"     // LoadIdsIntoRdl, RdlAppendNode, RdlClear, RdlRemoveLastParagraph
#include "nn_ui_pulse.h" // G_HudChromeColorRgba (DAT_00679bb4)
#include "win3d.h"

namespace IntroPageUtil
{
    void AppendNodeClone(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(Rdl, Temp->Clone());
        delete Temp;
    }

    void LoadIds(RenderDisplayList& Rdl, unsigned Ids)
    {
        RdlClear(Rdl);
        LoadIdsIntoRdl(Ids, Rdl);
    }

    void LoadIdsCut(RenderDisplayList& Rdl, unsigned Ids)
    {
        RdlClear(Rdl);
        if (LoadIdsIntoRdl(Ids, Rdl))
            RdlRemoveLastParagraph(Rdl);
    }

    void LoadStyledIds(RenderDisplayList& Rdl, unsigned short Style, unsigned Ids)
    {
        RdlClear(Rdl);
        {
            StyleNode const Node(Style);
            AppendNodeClone(Rdl, Node);
        }
        if (LoadIdsIntoRdl(Ids, Rdl))
            RdlRemoveLastParagraph(Rdl);
    }

    TextRenderAttributes PackTextAttributes(unsigned char A, unsigned char B, unsigned char C,
                                            unsigned char D, unsigned long Rgb)
    {
        unsigned char const Flags = static_cast<unsigned char>(
            (A & 1u) | ((B & 1u) << 1) | ((C & 1u) << 2) | ((D << 3) & 0xf8u));
        TextRenderAttributes Attrs;
        Attrs.Packed = static_cast<unsigned long>(Flags) | ((Rgb & 0xffffffu) << 8);
        return Attrs;
    }

    void HideWin3D(Win3D* Object)
    {
        Object->Flags = static_cast<unsigned char>(Object->Flags & 0xfc);
    }

    void ShowWin3D(Win3D* Object)
    {
        Object->Flags = static_cast<unsigned char>(Object->Flags | 3);
    }

    unsigned long const* ChromeColor()
    {
        return reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba);
    }

    int EdgeCount(std::vector<float> const& Edges)
    {
        return static_cast<int>(Edges.size());
    }
}
