// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "page_account_select.h"

#include "globals.h"          // GetClientFrameTimeCapped (FUN_0042d680)
#include "ids_rdl.h"          // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode, RdlClear
#include "menu_ui_init.h"     // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot
#include "nn_error.h"         // NnErrorRunModal (FUN_00473cb0)
#include "nn_ui_pulse.h"      // G_HudChromeColorRgba (DAT_00679bb4)
#include "tooltip_hover.h"    // FToolTipHoverInfo
#include "ui_rich_text_win.h"
#include "ui_text_edit.h"
#include "ui_text_msg_button.h"
#include "win3d_immediate.h"  // Win3DVec3Add (FUN_00423b30)

#include "common_net.h"       // CPlayerAccount
#include "common_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <vector>

namespace
{
    // DAT_0067aa10.
    PageAccountSelect G_PageAccountSelect;

    // u"0123456789-ABCDEF" (0x00614c74), handed to FUN_0057c860 with a count of 0x12 -- the
    // seventeen characters and the terminator.
    wchar_t const kAccountIdChars[] = L"0123456789-ABCDEF";

    // The idiom FUN_005727f0 spells at every RDL append: the node cloned (slot +0x1c), the
    // clone pushed by FUN_0046c800 (whose cell clones it again), the first clone deleted
    // (FUN_0045eeb0 / the inline slot-0 call).
    void AppendNodeClone(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(Rdl, Temp->Clone());
        delete Temp;
    }

    // 0x00572909..0x0057294b / 0x00572990..0x005729d3: the list emptied (FUN_0046c860 over
    // begin..end), StyleNode(0x9ca4) appended, the IDS text loaded and its trailing paragraph
    // stripped when the load worked.
    void LoadStyledIds(RenderDisplayList& Rdl, unsigned Ids)
    {
        RdlClear(Rdl);
        {
            StyleNode const Style(0x9ca4);
            AppendNodeClone(Rdl, Style);
        }
        if (LoadIdsIntoRdl(Ids, Rdl))
            RdlRemoveLastParagraph(Rdl);
    }

    void HideWin3D(Win3D* Object)
    {
        Object->Flags = static_cast<unsigned char>(Object->Flags & 0xfc);
    }

    void ShowWin3D(Win3D* Object)
    {
        Object->Flags = static_cast<unsigned char>(Object->Flags | 3);
    }
}

PageAccountSelect::PageAccountSelect()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , BoxHome{0.0f, 0.0f, 0.0f}
    , AccountId(nullptr)
    , SelectServer(nullptr)
    , Back(nullptr)
    , Explain(nullptr)
    , AccountBox(nullptr)
{
    // FUN_005726c0: FUN_00591ae0(FUN_00594580(+0x8c)) -- the page data pointer is taken before
    // the descriptor is built, as here -- then FUN_00591e30 on +0x8c, +0xe0..+0xf0 = 0, the two
    // RenderDisplayLists (+0xf4 / +0x100: allocator byte, a fresh 0xc-byte sentinel, size 0),
    // +0xb4 = 0 and the three vftables. +0xb8..+0xdc are not written; the static object starts
    // zeroed, which is what the initialisers above reproduce.
}

PageAccountSelect::~PageAccountSelect()
{
    // FUN_00573470: vftables back, +0x100 then +0xf4 emptied and their sentinels freed,
    // +0xe0..+0xf0 = 0, FUN_00591ef0 on +0x8c, FUN_00591ba0 -- the member destructors in reverse
    // declaration order, then the base.
}

bool PageAccountSelect::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_005727f0. MasterWin::Create (FUN_00591cd0) with no payload; false goes straight back.
    if (!UIPage::MasterWin::Create(NewParent, nullptr))
        return false;
    Result = 0;

    // 0x0057282d..0x00572887: the five widgets through the IUIPage face (slot 4).
    AccountId = static_cast<UiTextEdit*>(FindWidget("AccountID"));
    SelectServer = static_cast<UiTextMsgButton*>(FindWidget("SelectServer"));
    Back = static_cast<UiTextMsgButton*>(FindWidget("Back"));
    Explain = static_cast<UiRichTextWin*>(FindWidget("Explain"));
    AccountBox = FindWidget("AccountBoxShort");

    // 0x0057288d..0x005728c7: slot 46 (+0xb8) on each, +4 of the record it returns.
    AccountId->GetToolTipHoverInfo()->IdsAlt = 0xaca;
    SelectServer->GetToolTipHoverInfo()->IdsAlt = 0xace;
    Back->GetToolTipHoverInfo()->IdsAlt = 0xac6;

    // 0x005728bf..0x005728ce: FUN_005760d0(0xf) on the Event::Handler face.
    RegisterEvent(0xf);

    LoadStyledIds(ExplainText, 0x60005); // 0x005728d3..0x00572953
    LoadStyledIds(Text601eb, 0x601eb);   // 0x00572956..0x005729e1

    // 0x005729e4..0x00572b8a: the key field -- fixed style, no IME, the style for the fixed
    // list, a centred prefix of one space, 0x28 characters at most, the chrome frame, the
    // hex-and-dash allow list shown upper case, '-' for anything refused.
    AccountId->SetFixedStyle(true);   // FUN_0057bdd0(1)
    AccountId->SetImeEnabled(false);  // FUN_0057be30(0)
    {
        StyleNode const Style(0x9ca4);
        AppendNodeClone(AccountId->FixedStyle, Style); // +0x4fc
    }
    {
        JustifyNode const Justify(TextJustifyCenter);  // JustifyNode(1)
        AppendNodeClone(AccountId->Prefix, Justify);    // +0x4dc
    }
    {
        TextNode const Space(L" ", -1);                  // DAT_005cef90
        AppendNodeClone(AccountId->Prefix, Space);
    }
    AccountId->SetMaxLength(0x28);                                                    // FUN_0057bdb0
    AccountId->SetDrawFrame(true);                                                    // FUN_00579860
    AccountId->SetFrameColor(reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba)); // FUN_00579870
    AccountId->SetAlignMode(2);                                                       // FUN_00579a60
    AccountId->AddFilterChars(kAccountIdChars, 0x12);                                 // FUN_0057c860
    AccountId->SetReplacementChar(L'-');                                              // FUN_0057c920
    AccountId->SetCaseMode(1);                                                        // FUN_0057c840
    AccountId->SetFilterIsAllowList(true);                                            // FUN_0057c850

    // 0x00572b8f..0x00572bcf: the explanation shows the +0xf4 list by reference.
    Explain->ClearContent(); // FUN_0057b370
    {
        RDLRefNode Ref(ExplainText);
        Explain->AppendNode(Ref); // FUN_0057b470
    }
    Explain->SetAutoScrollbar(true); // FUN_005799f0(1)

    // 0x00572bd4..0x00572c1c: a machine key generated by CPlayerAccount goes into the field,
    // widened with mbstowcs over strlen + 1 characters (retail's buffer is _alloca'd). The
    // frame slot for the key is 0x28 bytes and a key of 0x28 characters puts its terminator in
    // the next local; the port's array holds it.
    char TextKey[0x28 + 1];
    if (CPlayerAccount::GenerateTextKey(TextKey))
    {
        size_t const Count = strlen(TextKey) + 1;
        std::vector<wchar_t> Wide(Count);
        mbstowcs(Wide.data(), TextKey, Count);
        AccountId->SetText(Wide.data(), nullptr); // FUN_0057c0e0(text, 0)
    }
    AccountId->Activate(); // FUN_0057c750

    // 0x00572c2c..0x00572c9c: the slide starts at -1.5, the buttons' surfaces frozen, the key
    // field and the explanation hidden until the slide ends, the box's home position kept.
    SlideT = -1.5;
    bLeaving = 0;
    LeaveDirection = 0;
    Back->SetSkipUpload(true);          // FUN_00579a90(1)
    SelectServer->SetSkipUpload(true);
    HideWin3D(AccountId);
    HideWin3D(Explain);
    memcpy(BoxHome, AccountBox->Position, sizeof(BoxHome));
    return true;
}

void PageAccountSelect::Destroy()
{
    // FUN_005727b0: FUN_005760f0(0xf) on the Event::Handler face, MasterWin::Destroy
    // (FUN_00591de0), then the five widget pointers cleared.
    UnregisterEvent(0xf);
    UIPage::MasterWin::Destroy();
    AccountId = nullptr;
    SelectServer = nullptr;
    Back = nullptr;
    Explain = nullptr;
    AccountBox = nullptr;
}

void PageAccountSelect::Update()
{
    // LAB_00572cc0. 0x00572cc6..0x00572cd7: FCOMP 1.0, TEST AH,0x41 / JP -- the slide-in runs
    // unless SlideT > 1.0 (or unordered).
    if (!(SlideT > 1.0) && bLeaving == 0)
    {
        // 0x00572ceb..0x00572d05: the frame time, capped at 0.5 (FCOM 0.5, TEST AH,0x41 / JNZ
        // keeps it when not greater).
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.5 + SlideT;

        // 0x00572d29..0x00572d3a: FCOM 0.0, TEST AH,5 / JP -- still sliding while SlideT < 0.
        if (SlideT < 0.0)
        {
            // 0x00572d40..0x00572dc7: Back offset (t, 0, 0), SelectServer (-t, 0, 0) through
            // slot 42 property 6, the box lifted by t (FUN_00423b30) through slot 5.
            float const T = static_cast<float>(SlideT);
            float BackOffset[3] = {T, 0.0f, 0.0f};
            float ServerOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            float const Lift[3] = {0.0f, T, 0.0f};
            Back->SetProperty(6, reinterpret_cast<uintptr_t>(BackOffset), 0);
            SelectServer->SetProperty(6, reinterpret_cast<uintptr_t>(ServerOffset), 0);
            float BoxPosition[3];
            Win3DVec3Add(BoxPosition, BoxHome, Lift);
            AccountBox->SetPosition(BoxPosition);
        }
        else
        {
            // 0x00572dcc..0x00572e72: everything in place and live, SlideT parked at 2.0.
            float ZeroOffset[3] = {0.0f, 0.0f, 0.0f};
            Back->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            SelectServer->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            AccountBox->SetPosition(BoxHome);
            ShowWin3D(Explain);
            ShowWin3D(AccountId);
            Back->SetSkipUpload(false);
            SelectServer->SetSkipUpload(false);
            AccountId->SetToolTipEnabled(true); // FUN_00579890(1)
            SelectServer->SetToolTipEnabled(true);
            Back->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        // 0x00572e7c..0x00572ea9: the slide sound while -0.8 < SlideT < -0.1 (FCOMP -0.8,
        // TEST AH,0x41 / JNZ; FCOMP -0.1, TEST AH,5 / JP).
        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b); // FUN_00564570(0x1b)
    }

    // 0x00572eac..0x00572fe1: leaving -- SlideT runs down from 0 at 1.5 per second (no cap).
    if (bLeaving != 0 && LeaveDirection != 0)
    {
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;
        float const T = static_cast<float>(SlideT);
        float BackOffset[3] = {T, 0.0f, 0.0f};
        float ServerOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        Back->SetProperty(6, reinterpret_cast<uintptr_t>(BackOffset), 0);
        SelectServer->SetProperty(6, reinterpret_cast<uintptr_t>(ServerOffset), 0);
        // 0x00572f3f..0x00572f71: FUN_00422b80(home.x, t + home.y, home.z) through slot 5.
        float BoxPosition[3] = {BoxHome[0], T + BoxHome[1], BoxHome[2]};
        AccountBox->SetPosition(BoxPosition);

        // 0x00572f74..0x00572f85: FCOMP -0.9, TEST AH,0x41 / JP -- done once SlideT <= -0.9.
        if (SlideT <= -0.9)
        {
            HideWin3D(Back);
            HideWin3D(Explain);
            HideWin3D(SelectServer);
            HideWin3D(AccountId);
            HideWin3D(AccountBox);
            if (LeaveDirection == 1)
                Result = 5;
            else if (LeaveDirection == -1)
                Result = 1;
            bLeaving = 0;
        }
    }

    // 0x00572fe8..0x00573009: Win3D's child walk (FUN_005a17b0 inlined).
    Win3D::Update();
}

bool PageAccountSelect::Slot23(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_00573010.
    if (Msg == 0x3004)
    {
        PlayOrRewindUiSoundSlot(0); // FUN_005645c0(0)
        if (A == 0x3e9) // "Back" (0x005730fd)
        {
            BeginLeave(-1);
            return true;
        }
        if (A != 0x3ea) // "SelectServer" (0x00573053)
            return false;
        return SubmitTypedKey();
    }
    if (Msg == 0xf003) // 0x005732dd
    {
        BeginLeave(-1);
        return true;
    }
    if (Msg == 0xf004) // 0x005731af
        return SubmitTypedKey();
    return false;
}

bool PageAccountSelect::SubmitTypedKey()
{
    // 0x00573053..0x005730c7 / 0x005731af..0x00573227: the typed key, lower-cased (towlower
    // until the terminator), narrowed with wcstombs over wcslen + 1 characters (retail's
    // buffer is _alloca'd), and handed to CPlayerAccount::SetTextKey.
    wchar_t Typed[0x2a];
    AccountId->GetText(Typed, 0x29); // FUN_0057c2c0
    for (wchar_t* Ch = Typed; *Ch != 0; ++Ch)
        *Ch = static_cast<wchar_t>(towlower(*Ch));
    size_t const Count = wcslen(Typed) + 1;
    std::vector<char> Narrow(Count);
    wcstombs(Narrow.data(), Typed, Count);
    if (!CPlayerAccount::SetTextKey(Narrow.data()))
    {
        // 0x005732ba..0x005732cf: the "invalid key" error, run modally; the page stays.
        NnErrorRunModal(0x76a, false, nullptr); // FUN_00473cb0(0x76a, 0, 0)
        LeaveDirection = 0;
        return true;
    }
    BeginLeave(1);
    return true;
}

bool PageAccountSelect::OnEvent(unsigned MsgId)
{
    // LAB_005733b0 (this = the +0x80 face): event 0xf leaves back to the main menu; anything
    // else is FUN_0053f330 (false).
    if (MsgId != 0xf)
        return UIPage::MasterWin::OnEvent(MsgId);
    BeginLeave(-1);
    return true;
}

void PageAccountSelect::SetToolTipsEnabled(bool bEnabled)
{
    // FUN_00573370.
    AccountId->SetToolTipEnabled(bEnabled);
    SelectServer->SetToolTipEnabled(bEnabled);
    Back->SetToolTipEnabled(bEnabled);
}

void PageAccountSelect::BeginLeave(int Direction)
{
    bLeaving = 1;
    SlideT = 0.0;
    Back->bInteractive = 0;          // +0x498
    SelectServer->bInteractive = 0;
    Back->SetSkipUpload(true);       // FUN_00579a90(1)
    SelectServer->SetSkipUpload(true);
    HideWin3D(AccountId);
    HideWin3D(Explain);
    PlayUiSoundSlot(0x1b);           // FUN_00564570(0x1b)
    SetToolTipsEnabled(false);       // FUN_00573370(0), inline at 0x00573167 / 0x0057340e
    LeaveDirection = Direction;
}

PageAccountSelect& GetPageAccountSelect()
{
    return G_PageAccountSelect;
}
