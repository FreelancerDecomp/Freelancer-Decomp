// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_OfferDialog. See nn_offer_dialog.h.

#include "nn_offer_dialog.h"

#include "ids_rdl.h"         // RdlAppendNode, AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "knowledge_map.h"   // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "misc_util.h"       // FmtStrSprintf (FUN_0041c970)
#include "nn_modal.h"        // G_bNnScreenUp (DAT_0067dcc8), NnFloatBits
#include "nn_text_scratch.h" // G_NnTextScratchB (DAT_0066fc60), G_NnTextScratchCapacity
#include "rich_text_win.h"
#include "win3d.h"
#include "win_factory.h"

#include "common_text.h"     // FmtStr, RenderDisplayList, StyleNode

#include <stdint.h>
#include <wchar.h>

// Retail DAT_00672108 -- WINFACTORY<NN_OfferDialog> static (LAB_004a60f0 -> FUN_004a6960; Create
// FUN_004a69d0; atexit LAB_004a6110 -> FUN_00579380).
WINFACTORY<NnOfferDialog> G_NnOfferDialogFactory("NN_OfferDialog");

namespace
{
    char const kOfferDialogBackgroundMesh[] =
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp";            // 0x005ce994
    char const kOfferDialogCloseMesh[] = "Interface\\TextOffer\\x.3db";   // 0x005d4584
    char const kOfferDialogSelectSound[] = "ui_select_item";              // 0x005ced30

    // The Flags (+0x6c) edits FUN_004a6630 makes: AND 0xfc hides, OR 3 shows.
    void HideControl(WinControl* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags & 0xfc);
    }

    void ShowControl(WinControl* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags | 3);
    }
}

NnOfferDialog::NnOfferDialog()
    : Callback(nullptr)
    , Context(nullptr)
    , bSingleButton(0)
    , AcceptButton(nullptr)
    , AcceptText(nullptr)
    , CloseButton(nullptr)
    , Title(nullptr)
    , OfferText(nullptr)
    , Field_0x350(0)
    , DeclineButton(nullptr)
    , DeclineText(nullptr)
    , Close2Button(nullptr)
    , CloseText(nullptr)
{
    // FUN_004a69d0 0x004a6a09..0x004a6a38: FUN_0059f860, the vptrs, DAT_0067dcc8 = 1,
    // +0x338 = 0. The other members are written by Create / Open before anything reads them;
    // the port zeroes them here.
    Pad339[0] = 0;
    Pad339[1] = 0;
    Pad339[2] = 0;
    G_bNnScreenUp = true;
}

NnOfferDialog::~NnOfferDialog()
{
    // FUN_004a6140: vptrs back to NN_OfferDialog's, DAT_0067dcc8 = 0; then FUN_00576010 (the
    // Event::Handler base) and FUN_0059fa50 (WinControl) run as the base-class destructors.
    G_bNnScreenUp = false;
}

bool NnOfferDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a61b0 (RET 8). NewParent is not read: Win3D::Create gets the window's own +0x04.
    (void)NewParent;
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    // 0x004a61b4..0x004a622f: FUN_005a0db0("OfferDialogBackground", mesh, {0, 0, 0}, 0,
    // {0, 0, 0}, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, {0, 0, 0}).
    CreateStaticControl("OfferDialogBackground", kOfferDialogBackgroundMesh, Zero, nullptr, Zero,
                        0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    // 0x004a6234..0x004a6296: FUN_005a0e30("OfferDialogTitle", "PageHeader",
    // {-0.418, 0.253, 0}, 0, 0.839, 0.06, 1.2, 1000, 1, 0, 1) -> +0x348.
    float const TitlePosition[3] = {NnFloatBits(0xbed60419), NnFloatBits(0x3e818937), 0.0f};
    Title = CreateTextControl("OfferDialogTitle", "PageHeader", TitlePosition, nullptr,
                              NnFloatBits(0x3f56c8b4), NnFloatBits(0x3d75c28f),
                              NnFloatBits(0x3f99999a), 0x3e8, 1, 0, 1);

    // 0x004a627e..0x004a62cb: FUN_005a10e0("OfferDialogOfferText", 0.785, 0.34,
    // {-0.4, 0.181, 0}, 1) -> +0x34c.
    float const TextOffset[3] = {NnFloatBits(0xbecccccd), NnFloatBits(0x3e395810), 0.0f};
    OfferText = CreateRichTextWin("OfferDialogOfferText", NnFloatBits(0x3f48f5c3),
                                  NnFloatBits(0x3eae147b), TextOffset, true);

    // 0x004a62b9..0x004a6349: FUN_005a0d00("OfferDialogAcceptButton", "ui_select_item",
    // {-0.142, -0.21, 0}, {0.09, 0.026, 0}, 0, 0, 0x332, 0, 0) -> +0x33c.
    float const ButtonExtents[3] = {NnFloatBits(0x3db851ec), NnFloatBits(0x3cd4fdf4), 0.0f};
    float const AcceptPosition[3] = {NnFloatBits(0xbe116873), NnFloatBits(0xbe570a3d), 0.0f};
    AcceptButton = CreateWireButtonControl("OfferDialogAcceptButton", kOfferDialogSelectSound,
                                           AcceptPosition, ButtonExtents, 0, 0, 0x332, 0, 0);

    // 0x004a6317..0x004a6391: FUN_005a0c90("OfferDialogCloseButton", "Interface\TextOffer\x.3db",
    // 0, "ui_select_item", 0, {0.397, 0.231, 0}, 0, {0.018, 0.018, 0}, 0, 0, 0, 0, 0x409, 0, 0)
    // -> +0x344.
    float const ClosePosition[3] = {NnFloatBits(0x3ecb4396), NnFloatBits(0x3e6c8b44), 0.0f};
    float const CloseExtents[3] = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
    CloseButton = CreateButtonControl("OfferDialogCloseButton", kOfferDialogCloseMesh, nullptr,
                                      kOfferDialogSelectSound, nullptr, ClosePosition, nullptr,
                                      CloseExtents, 0, 0, 0, 0, 0x409, 0, 0);

    // 0x004a6384..0x004a640f: FUN_005a0d00("OfferDialogDeclineButton", "ui_select_item",
    // {0.13, -0.21, 0}, {0.09, 0.026, 0}, 0, 0, 0x333, 0, 0) -> +0x354.
    float const DeclinePosition[3] = {NnFloatBits(0x3e051eb8), NnFloatBits(0xbe570a3d), 0.0f};
    DeclineButton = CreateWireButtonControl("OfferDialogDeclineButton", kOfferDialogSelectSound,
                                            DeclinePosition, ButtonExtents, 0, 0, 0x333, 0, 0);

    // 0x004a63e2..0x004a644e: FUN_005a0e30("OfferDialogAcceptText", "ButtonText",
    // {-0.23, -0.188, 0}, 0, 0.172, 0.06, 1.5, 0x4ad, 1, 0, 1) -> +0x340.
    float const AcceptTextPosition[3] = {NnFloatBits(0xbe6b851f), NnFloatBits(0xbe408312), 0.0f};
    AcceptText = CreateTextControl("OfferDialogAcceptText", "ButtonText", AcceptTextPosition,
                                   nullptr, NnFloatBits(0x3e3020c5), NnFloatBits(0x3d75c28f),
                                   NnFloatBits(0x3fc00000), 0x4ad, 1, 0, 1);

    // 0x004a6432..0x004a64a5: FUN_005a0e30("OfferDialogDeclineText", "ButtonText",
    // {0.041, -0.188, 0}, 0, 0.177, 0.06, 1.5, 0x548, 1, 0, 1) -> +0x358.
    float const DeclineTextPosition[3] = {NnFloatBits(0x3d27ef9e), NnFloatBits(0xbe408312),
                                          0.0f};
    DeclineText = CreateTextControl("OfferDialogDeclineText", "ButtonText", DeclineTextPosition,
                                    nullptr, NnFloatBits(0x3e353f7d), NnFloatBits(0x3d75c28f),
                                    NnFloatBits(0x3fc00000), 0x548, 1, 0, 1);

    // 0x004a6482..0x004a64fc: FUN_005a0d00("OfferDialogClose2Button", "ui_select_item",
    // {0, -0.21, 0}, {0.09, 0.026, 0}, 0, 0, 0x334, 0, 0) -> +0x35c.
    float const Close2Position[3] = {0.0f, NnFloatBits(0xbe570a3d), 0.0f};
    Close2Button = CreateWireButtonControl("OfferDialogClose2Button", kOfferDialogSelectSound,
                                           Close2Position, ButtonExtents, 0, 0, 0x334, 0, 0);

    // 0x004a64e0..0x004a6532: FUN_005a0e30("OfferDialogCloseText", "ButtonText",
    // {-0.09, -0.19, 0}, 0, 0.179, 0.06, 1.5, 0x3a1, 1, 0, 1) -> +0x360.
    float const CloseTextPosition[3] = {NnFloatBits(0xbdb851ec), NnFloatBits(0xbe428f5c), 0.0f};
    CloseText = CreateTextControl("OfferDialogCloseText", "ButtonText", CloseTextPosition,
                                  nullptr, NnFloatBits(0x3e374bc7), NnFloatBits(0x3d75c28f),
                                  NnFloatBits(0x3fc00000), 0x3a1, 1, 0, 1);

    // 0x004a6530..0x004a6557: slot 5 SetPosition({0, 0, -1.288}).
    float const WindowPosition[3] = {0.0f, 0.0f, NnFloatBits(0xbfa4dd2f)};
    SetPosition(WindowPosition);

    // 0x004a655a..0x004a6562: FUN_005760d0(0xf) on the Event::Handler face.
    RegisterEvent(0xf);

    // 0x004a6567..0x004a6579: FUN_004b5060 with the window's own +0x04 parent, SETNZ.
    return Win3D::Create(Parent, Data);
}

intptr_t NnOfferDialog::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a6590 (RET 0xc). Nothing reaches WinControl's switch.
    (void)B;
    if (Msg != 0x26)
        return 0;
    // 0x004a6597..0x004a65b1 / 0x004a65e0..0x004a65fd: Close, Decline or Close2 clicked ->
    // the callback (when set) with 1; true.
    if (A == reinterpret_cast<uintptr_t>(CloseButton) ||
        A == reinterpret_cast<uintptr_t>(DeclineButton) ||
        A == reinterpret_cast<uintptr_t>(Close2Button))
    {
        if (Callback != nullptr)
            Callback(Context, 1);
        return 1;
    }
    // 0x004a65b3..0x004a65d8: Accept clicked -> the callback (when set) with 0; true.
    if (A != reinterpret_cast<uintptr_t>(AcceptButton))
        return 0;
    if (Callback != nullptr)
        Callback(Context, 0);
    return 1;
}

bool NnOfferDialog::OnEvent(unsigned MsgId)
{
    // LAB_004a6600 (RET 4): 0xf with +0x338 clear -> the callback (when set) with 1; true.
    if (MsgId != 0xf)
        return false;
    if (bSingleButton != 0)
        return false;
    if (Callback != nullptr)
        Callback(Context, 1);
    return true;
}

void NnOfferDialog::Open(int Type, FmtStr const* Text, bool bInSingleButton,
                         FNnOfferDialogCallback InCallback, void* InContext)
{
    // FUN_004a6630 (thiscall, RET 0x14).
    // 0x004a664a..0x004a6665: every offer type but 1 marks the text's places known.
    if (Type != 1)
        KnowledgeMapRevealFmtStr(Text);
    // 0x004a6668..0x004a6676
    Context = InContext;
    Callback = InCallback;

    // 0x004a667c..0x004a6743: the text formatted into DAT_0066fc60, read into a display list
    // after a StyleNode 0x9ca4 (a clone of the node, the list takes a clone of that, the first
    // clone is deleted), then the offer text starts at the top with the new content.
    FmtStrSprintf(Text, G_NnTextScratchB, G_NnTextScratchCapacity);
    RenderDisplayList Body;
    {
        StyleNode const Style(0x9ca4);
        RDLNode* const Temp = Style.Clone();
        RdlAppendNode(Body, Temp->Clone());
        delete Temp;
    }
    AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                          Body, false);
    OfferText->SetScrollOrigin(0, 0); // FUN_005798b0
    OfferText->ClearContent();        // FUN_0057b370
    OfferText->AppendContent(Body);   // FUN_0057b3e0

    // 0x004a6743..0x004a6779: the title by type -- slot 42 (0x1e, ids, 0) with
    // {0x546, 0x650, 0x547, 0x651, 0x651}[Type] (a stack table; Type is not range-checked).
    unsigned const TitleIds[5] = {0x546, 0x650, 0x547, 0x651, 0x651};
    Title->SetProperty(0x1e, TitleIds[Type], 0);

    // 0x004a677f..0x004a6804: types 0, 1 and 3 show Accept / Decline and their texts and hide
    // Close2; types 2 and 4 the other way round; any other type leaves the buttons alone.
    if (Type == 0 || Type == 3 || Type == 1)
    {
        HideControl(Close2Button);
        HideControl(CloseText);
        ShowControl(AcceptButton);
        ShowControl(AcceptText);
        ShowControl(DeclineButton);
        ShowControl(DeclineText);
    }
    else if (Type == 2 || Type == 4)
    {
        HideControl(AcceptButton);
        HideControl(AcceptText);
        HideControl(DeclineButton);
        HideControl(DeclineText);
        ShowControl(Close2Button);
        ShowControl(CloseText);
    }

    // 0x004a6807..0x004a68ea: a single button -- Decline, its text and Close hidden, +0x338 = 1,
    // Accept moved to {0, -0.21, 0} (slot 42 cmd 7 reads the old offset, cmd 6 sets the new) and
    // its text moved by the same amount. Each difference and sum is stored as a float.
    if (bInSingleButton)
    {
        HideControl(DeclineButton);
        HideControl(DeclineText);
        HideControl(CloseButton);
        bSingleButton = 1;

        float AcceptOld[3];
        AcceptButton->SetProperty(7, reinterpret_cast<uintptr_t>(AcceptOld), 0);
        float const AcceptNew[3] = {0.0f, NnFloatBits(0xbe570a3d), 0.0f};
        AcceptButton->SetProperty(6, reinterpret_cast<uintptr_t>(AcceptNew), 0);
        float const DeltaZ = AcceptNew[2] - AcceptOld[2];
        float const DeltaY = AcceptNew[1] - AcceptOld[1];
        float const DeltaX = AcceptNew[0] - AcceptOld[0];

        float TextOld[3];
        AcceptText->SetProperty(7, reinterpret_cast<uintptr_t>(TextOld), 0);
        float const TextNew[3] = {DeltaX + TextOld[0], TextOld[1] + DeltaY, TextOld[2] + DeltaZ};
        AcceptText->SetProperty(6, reinterpret_cast<uintptr_t>(TextNew), 0);
    }
    // 0x004a68f0..0x004a6938: the display list freed -- ~RenderDisplayList.
}
