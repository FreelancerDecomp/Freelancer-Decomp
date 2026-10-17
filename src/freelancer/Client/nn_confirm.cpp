// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Confirm (NN_LoadSave.cpp). See nn_confirm.h.

#include "nn_confirm.h"

#include "font_string.h"        // FFontString (+0x28 LayoutGen, +0x30 bMultiLine)
#include "ids_strings.h"        // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "nn_modal.h"           // NnFloatBits
#include "nn_text_control.h"
#include "win3d.h"
#include "win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "dacom.h"              // FDUMP

#include <stdio.h>
#include <wchar.h>

// Retail DAT_00671ee8 -- WINFACTORY<NN_Confirm> static (LAB_004882d0 -> FUN_00488db0; Create
// FUN_00488e20; atexit LAB_004882f0 -> FUN_00579380).
WINFACTORY<NnConfirm> G_NnConfirmFactory("NN_Confirm");

namespace
{
    char const kConfirmFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4window.cmp"; // 005d04a8
    char const kNnLoadSaveCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_LoadSave.cpp"; // 005d1e40
}

NnConfirm::NnConfirm()
    : Frame(nullptr)
    , Text(nullptr)
    , YesBox(nullptr)
    , NoBox(nullptr)
    , YesText(nullptr)
    , NoText(nullptr)
{
    // FUN_00488e20 00488e5b..00488e77: FUN_0059f860 and the vptrs only; Create writes every
    // field before anything reads it (the port zeroes them above).
}

NnConfirm::~NnConfirm()
{
    // FUN_00488320: DEC DAT_0067dccc, FUN_0059d8c0(0); then the Event::Handler base dtor
    // FUN_00576010 and WinControl's FUN_0059fa50 run as the base-class destructors. No
    // FUN_00577c30 here -- Create pushed no input focus.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
}

bool NnConfirm::Create(Win3D* NewParent, void* Data)
{
    // FUN_004883b0 (RET 8). EDI = the payload's text, EBP = the payload.
    FNnConfirmPayload const* const Payload = static_cast<FNnConfirmPayload const*>(Data);
    wchar_t const* const Question = Payload->Text;

    // 0x004883bd..0x00488433: FUN_00423b30(&local_c, &{-0.018, 0, -1}, &{0.02, 0.015, 0}) --
    // the frame position, component-wise sum.
    float const Base[3] = {NnFloatBits(0xbc9374bc), 0.0f, -1.0f};             // local_3c
    float const Delta[3] = {NnFloatBits(0x3ca3d70a), NnFloatBits(0x3c75c28f), 0.0f}; // local_30
    float const Zero[3] = {0.0f, 0.0f, 0.0f};                                  // local_24 / local_18

    float const FramePosition[3] = {Base[0] + Delta[0], Base[1] + Delta[1], Base[2] + Delta[2]};
    // 0x00488436..0x00488476: FUN_005a0db0("Confirm_deleteBox", mesh, pos, 0, {0,0,0}, 0, 0, 0,
    // 1, 1, 0, 1, 0, 0, 0, {0,0,0}) -> +0x330.
    Frame = CreateStaticControl("Confirm_deleteBox", kConfirmFrameMesh, FramePosition, nullptr,
                                Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);

    float const kRowOffsetY = NnFloatBits(0x3d4ccccd);  // DAT_005cb88c 0.05
    float const kTextOffsetX = NnFloatBits(0x3ccccccd); // DAT_005d15b4 0.025
    float const kYesOffsetX = NnFloatBits(0x3dae147b);  // DAT_005d1f84 0.085
    float const kNoOffsetX = NnFloatBits(0x3e051eb8);   // DAT_005d0434 0.13

    // 0x0048846c..0x004884cf: FUN_00422b80(Base.x + 0.025, Base.y + 0.05, Base.z), then
    // FUN_005a0e30("Confirm_delTxt", Style, pos, 0, 0.4, 0.12, 1.0, 0x4ad, 1, 0, 0) -> +0x334.
    float const TextPosition[3] = {Base[0] + kTextOffsetX, Base[1] + kRowOffsetY, Base[2]};
    Text = CreateTextControl("Confirm_delTxt", Payload->Style, TextPosition, nullptr,
                             NnFloatBits(0x3ecccccd), NnFloatBits(0x3df5c28f), 1.0f, 0x4ad, 1, 0,
                             0);

    // 0x004884d5..0x004884e9: the text's Font::String (+0x334) goes multi-line (+0x30 = 1, and
    // +0x28 = 0 to re-lay it out) unless it already is. No null test on the text.
    FFontString* const Font = static_cast<NnTextControl*>(Text)->GetFontString();
    if (Font->bMultiLine != 1)
    {
        Font->bMultiLine = 1;
        Font->LayoutGen = 0;
    }
    // 0x004884ec..0x004884f9: slot 42 (0x1d, question, 0).
    Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Question), 0);

    // 0x004884ff..0x00488583: "Confirm_delBtnYes" at (Base.x - 0.085, Base.y - 0.05, Base.z),
    // half extents {0.09, 0.021, 0}, no sound -> +0x338.
    float const ButtonExtents[3] = {NnFloatBits(0x3db851ec), NnFloatBits(0x3cac0831), 0.0f};
    float const YesPosition[3] = {Base[0] - kYesOffsetX, Base[1] - kRowOffsetY, Base[2]};
    YesBox = CreateWireButtonControl("Confirm_delBtnYes", nullptr, YesPosition, ButtonExtents, 0,
                                     0, 0, 0, 0);

    // 0x00488565..0x004885ef: "Confirm_delBtnNo" at (Base.x + 0.13, Base.y - 0.05, Base.z) ->
    // +0x33c.
    float const NoPosition[3] = {Base[0] + kNoOffsetX, Base[1] - kRowOffsetY, Base[2]};
    NoBox = CreateWireButtonControl("Confirm_delBtnNo", nullptr, NoPosition, ButtonExtents, 0, 0,
                                    0, 0, 0);

    // 0x004885d1..0x00488669: the captions on the boxes' positions -- "ButtonText", 0.27 x 0.03,
    // 1.0, ids 0x4dc (yes) / 0x4dd (no), 1, 0, 0 -> +0x340 / +0x344.
    YesText = CreateTextControl("Confirm_delBtnYesText", "ButtonText", YesPosition, nullptr,
                                NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0x4dc, 1,
                                0, 0);
    NoText = CreateTextControl("Confirm_delBtnNoText", "ButtonText", NoPosition, nullptr,
                               NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0x4dd, 1, 0,
                               0);

    // 0x0048869b..0x004886a8
    DrawLayer = 0x6e;
    DrawSubLayer = 7;
    // 0x004886af..0x004886ca
    RegisterEvent(0x13);
    RegisterEvent(0x12);
    RegisterEvent(0xf);
    RegisterEvent(0x11);
    // 0x004886d0: FUN_0059d8c0(this); 0x004886e6: INC DAT_0067dccc.
    Win3DSetKeyFocus(this);
    G_ModalCaptureCount = G_ModalCaptureCount + 1;

    // 0x004886eb: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void NnConfirm::Answer(unsigned bYes)
{
    if (Parent != nullptr)
        Parent->Slot23(0x3006, bYes, 0);
    ReleaseMouse();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnConfirm::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // JMP FUN_005a0760
}

bool NnConfirm::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004887d0 (RET 0xc): 0xf003 / 0xf004 go on to slot 42 with A and B; always false.
    if (Msg == 0xf003 || Msg == 0xf004)
        SetProperty(static_cast<int>(Msg), A, B);
    return false;
}

intptr_t NnConfirm::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00488800 (RET 0xc). Every path returns 0; nothing reaches WinControl's switch.
    (void)B;
    if (Msg == 0x26)
    {
        // 0x00488866..0x0048893a: which box was clicked.
        if (A == reinterpret_cast<uintptr_t>(YesBox))
        {
            Answer(1);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(NoBox))
        {
            Answer(0);
            return 0;
        }
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x5af,
              "Invalid button in confirmation dialog\n");
        return 0;
    }
    if (Msg == 0x42)
    {
        // 0x00488834..0x0048885c: slot 31 SetOffset with the three floats at A, slot 5
        // SetPosition(&+0x60).
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        return 0;
    }
    if (Msg == 0xf003)
    {
        // 0x0048881d..0x0048882b: the Event::Handler face's slot 0 with 0xf -- "no".
        OnEvent(0xf);
        return 0;
    }
    return 0;
}

bool NnConfirm::OnEvent(unsigned MsgId)
{
    // LAB_00488700 (RET 4): jump table 0x004887b4 over MsgId - 0xf.
    //   0xf, 0x12 -> no;  0x11, 0x13 -> yes;  0x10 and anything else -> not handled.
    switch (MsgId)
    {
    case 0xf:
    case 0x12:
        Answer(0);
        return true;
    case 0x11:
    case 0x13:
        Answer(1);
        return true;
    default:
        return false;
    }
}

namespace
{
    // The tail FUN_004889e0 and FUN_00488a80 share (0x00488a03..0x00488a65 /
    // 0x00488aef..0x00488b54).
    Win3D* RaiseConfirm(Win3D* Parent, FNnConfirmPayload* Payload)
    {
        if (Win3DFindByClassName("NN_Confirm") != nullptr) // FUN_0059d9c0
            return nullptr;
        Win3D* const Created = WinFactoryCreate("NN_Confirm", "NN_Confirm", Parent, Payload);
        if (Created == nullptr)
            return nullptr;
        NnConfirm* const Dialog = static_cast<NnConfirm*>(Created);
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        Dialog->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
        Dialog->CaptureMouse(); // slot 32
        return Created;
    }
}

Win3D* NnConfirmOpen(Win3D* Parent, wchar_t const* Text, char const* Style)
{
    // FUN_004889e0.
    FNnConfirmPayload Payload; // local_14 / local_10
    Payload.Text = Text;
    Payload.Style = Style;
    if (Style == nullptr)
        Payload.Style = "ConfirmText"; // 005d2254
    return RaiseConfirm(Parent, &Payload);
}

void NnConfirmOpenFormatted(Win3D* Parent, wchar_t const* Name, char const* Style)
{
    // FUN_00488a80 (0x1014 bytes of stack: the ids string at +0x14, the formatted text at
    // +0x814, 0x400 wchar_t each).
    wchar_t Format[0x400];
    wchar_t Formatted[0x400];
    // 0x00488a8a..0x00488aa0: FUN_004347e0(DAT_0067eca8, 0x75c, Format, 0x400).
    GetIdsStringWWithRawFallback(0x75c, Format, 0x400);
    // 0x00488aa5..0x00488aba: MSVCRT swprintf(Formatted, Format, Name).
    swprintf(Formatted, 0x400, Format, Name);

    // 0x00488ac0..0x00488aee: a null Style becomes "ConfirmPlayerText" (DAT_00611aec); the
    // following "null -> ConfirmText" test can no longer hold.
    FNnConfirmPayload Payload; // local_res0 / +4
    Payload.Text = Formatted;
    Payload.Style = Style;
    if (Payload.Style == nullptr)
        Payload.Style = "ConfirmPlayerText";
    RaiseConfirm(Parent, &Payload);
}
