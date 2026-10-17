// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail ServerFilterDialog -- the server browser's filter box. See server_filter_dialog.h.

#include "server_filter_dialog.h"
#include "msvcrt_compat.h" // MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "client_state.h"     // CallClientStateUpdateSlot (DAT_0067ecac vftable[14])
#include "globals.h"          // PumpMessages (FUN_005b0b60), GetClientFrameTimeCapped (FUN_0042d680)
#include "ids_rdl.h"          // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "nn_ui_pulse.h"      // G_HudChromeColorRgba (DAT_00679bb4)
#include "rich_text_win.h"
#include "tooltip_hover.h"    // FToolTipHoverInfo
#include "win3d.h"
#include "win_factory.h"

#include "common_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

// Retail DAT_0067a444 -- WINFACTORY<ServerFilterDialog> (LAB_005626e0 -> FUN_00563890; Create
// FUN_00563900; atexit LAB_00562700 -> FUN_00579380).
WINFACTORY<ServerFilterDialog> G_ServerFilterDialogFactory("ServerFilterDialog");

namespace
{
    // DAT_0067a440 -- LAB_00562710: FLD -0.2f (DAT_005e2098), FADD 0.03f (DAT_005d011c), FSTP.
    // The exact sum rounded once, as the x87 does.
    float const G_FilterLabelX =
        static_cast<float>(static_cast<double>(-0.200000003f) + static_cast<double>(0.0299999993f));

    // DAT_0067a43c -- LAB_00562730: FLD 0.28f (DAT_005e2080), FADD DAT_0067a440, FADD 0.03f,
    // FSTP. Both sums are exact in double, so the one rounding to float matches the x87's.
    float const G_FilterDataX = static_cast<float>(static_cast<double>(0.280000001f) +
                                                   static_cast<double>(G_FilterLabelX) +
                                                   static_cast<double>(0.0299999993f));

    // DAT_00614828 -- the five row descriptions. Row 0's Y (0x00614828 = 0.12f) is linked
    // data; every other field is a LAB_00562750 store (0x00562750..0x005628c6): X = -0.2f
    // (DAT_005e2098), Z = 0 (DAT_005e209c), LabelX = DAT_0067a440, DataX = DAT_0067a43c except
    // row 0's 0.12f (0x00614838).
    FServerFilterRowDesc const G_ServerFilterRows[5] = {
        {0.119999997f, 0.0f, -0.200000003f, G_FilterLabelX, 0.119999997f, 1, 0x60078, 0x1b58},
        {0.0599999987f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60074, 0x1b59},
        {0.0f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60073, 0x1b5a},
        {-0.0599999987f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60077, 0x1b5c},
        {-0.119999997f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60075, 0x1b5d},
    };

    // DAT_006148c8 -- the number in the rows' names ("FilterCheck%d" ...), linked as 1 and
    // raised by every BuildRow; nothing resets it, so each dialog gets fresh names.
    int G_ServerFilterRowCounter = 1;

    // PTR_s_ServerFilterDialog_006148cc -> 0x005e205c.
    char const* const kServerFilterDialogClass = "ServerFilterDialog";

    char const kCheckMesh[] = "Interface\\NeuroNet\\Inventory\\check.3db";                 // 005d4e20
    char const kCheckAltMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";     // 005cb0d0
    char const kCheckSound[] = "ui_shipview_select";                                      // 005d4e48
    char const kBackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db";          // 005d21b0
    char const kBackgroundMesh[] = "Interface\\intro\\objects\\MULTI_attentionreq'd.cmp"; // 005ce934
    char const kCancelMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db";                   // 005cf1f4
    char const kSelectSound[] = "ui_select_item";                                         // 005ced30

    // The idiom BuildRow spells for the edit box's two lists: the node cloned (slot +0x1c), the
    // clone pushed (the list cell clones it again), the first clone deleted.
    void AppendNodeClone(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(Rdl, Temp->Clone());
        delete Temp;
    }
}

ServerFilterDialog::ServerFilterDialog()
    : OkButton(nullptr)
    , CancelButton(nullptr)
    , Title(nullptr)
    , Target(nullptr)
{
    // FUN_00563900 0x0056393c..0x005639f2: WinControl ctor, the rows (LAB_00562a40), the
    // ServerFilter ctor at +0x378 (FUN_005628e0 inlined), +0x374 / +0x32c / +0x330 / +0x334 = 0.
}

ServerFilterDialog::~ServerFilterDialog()
{
    // FUN_00562f60: the vptrs, the rows through `eh vector destructor iterator` (LAB_00562a50
    // stores zero into each row of the object being destroyed), then WinControl's dtor
    // FUN_0059fa50. The ServerFilter at +0x378 has no destructor.
}

void FServerFilterDialogRow::Build(WinControl* Dialog, FServerFilterRowDesc const& Desc)
{
    // FUN_00562a60. The names go through FUN_00402180 (_vsnprintf into a 0x2000-byte buffer).
    char Name[0x2000];
    float const* const DialogOffset = Dialog->Offset; // +0xa8

    // 0x00562a90..0x00562b19: the toggle at {X, Y, Z}, half extents {0.012, 0.012, 0}, the row's
    // tool tip twice, then property 0x35 = 1.
    float const CheckOffset[3] = {Desc.X, Desc.Y, Desc.Z};
    float const CheckHalfExtents[3] = {0.0120000001f, 0.0120000001f, 0.0f}; // 0x3c449ba6
    _snprintf(Name, 0x2000, "FilterCheck%d", G_ServerFilterRowCounter);
    Check = Dialog->CreateToggleButtonControl(Name, kCheckMesh, kCheckAltMesh, kCheckSound,
                                              nullptr, CheckOffset, nullptr, CheckHalfExtents,
                                              0, 0, 0, 0, Desc.ToolTipIds,
                                              Desc.ToolTipIds); // FUN_005a0fb0
    Check->SetProperty(0x35, 1, 0);

    // 0x00562b1f..0x00562bd2: the label -- the row's IDS text behind StyleNode 0x5724.
    _snprintf(Name, 0x2000, "FilterLabel%d", G_ServerFilterRowCounter);
    Label = static_cast<RichTextWin*>(WinFactoryCreate(Name, "RichTextWin", Dialog, nullptr));
    {
        RenderDisplayList Text;
        LoadIdsIntoRdl(Desc.LabelIds, Text); // FUN_0057da40, result ignored
        Label->ClearContent();               // FUN_0057b370
        {
            StyleNode const Style(0x5724);
            Label->AppendNode(Style);        // FUN_0057b470
        }
        Label->AppendContent(Text);          // FUN_0057b3e0
    }                                        // FUN_0046ba90

    // 0x00562bd7..0x00562c1c: {LabelX, Y + 0.03 * 0.5, Z} plus the dialog's +0xa8, summed on
    // the FPU in that order and stored as floats.
    float const LabelOffset[3] = {
        static_cast<float>(static_cast<double>(Desc.LabelX) +
                           static_cast<double>(DialogOffset[0])),
        static_cast<float>((0.0299999993f * 0.5 + static_cast<double>(Desc.Y)) +
                           static_cast<double>(DialogOffset[1])),
        static_cast<float>(static_cast<double>(Desc.Z) + static_cast<double>(DialogOffset[2]))};
    Label->SetAttachOffset(LabelOffset); // FUN_00579c40

    // 0x00562c21..0x00562c48: 0.28 wide beside an edit box, else 0.38; 0.03 high.
    Label->SetNormSize(Desc.bHasData != 0 ? 0.280000001f : 0.379999995f, 0.0299999993f);

    // 0x00562c4d..0x00562c6d: slot 46's record +4 = the row's tool tip, the feed on, align 2.
    Label->GetToolTipHoverInfo()->IdsAlt = Desc.ToolTipIds;
    Label->SetToolTipEnabled(true); // FUN_00579890(1)
    Label->SetAlignMode(2);         // FUN_00579a60(2)

    if (Desc.bHasData == 0)
    {
        Data = nullptr; // 0x00562f05
    }
    else
    {
        // 0x00562c7d..0x00562f03: the edit box -- {DataX, Y + 0.015, Z} plus +0xa8, 0.1 x 0.03,
        // fixed style 0x9ca4, no IME, a centred one-space prefix, four characters, the chrome
        // frame, align 2.
        _snprintf(Name, 0x2000, "FilterData%d", G_ServerFilterRowCounter);
        Data = static_cast<RichTextEditWin*>(
            WinFactoryCreate(Name, "RichTextEditWin", Dialog, nullptr));
        float const DataOffset[3] = {
            static_cast<float>(static_cast<double>(Desc.DataX) +
                               static_cast<double>(DialogOffset[0])),
            static_cast<float>((0.0299999993f * 0.5 + static_cast<double>(Desc.Y)) +
                               static_cast<double>(DialogOffset[1])),
            static_cast<float>(static_cast<double>(Desc.Z) +
                               static_cast<double>(DialogOffset[2]))};
        Data->SetAttachOffset(DataOffset);                    // FUN_00579c40
        Data->SetNormSize(0.100000001f, 0.0299999993f);       // FUN_00579cc0
        Data->SetFixedStyle(true);                            // FUN_0057bdd0(1)
        Data->SetImeEnabled(false);                           // FUN_0057be30(0)
        {
            StyleNode const Style(0x9ca4);
            AppendNodeClone(Data->FixedStyle, Style);         // +0x4fc, FUN_0046c800
        }
        {
            JustifyNode const Justify(TextJustifyCenter);     // JustifyNode(1)
            AppendNodeClone(Data->Prefix, Justify);           // +0x4dc
        }
        {
            TextNode const Space(L" ", -1);                   // DAT_005cef90
            AppendNodeClone(Data->Prefix, Space);
        }
        Data->SetMaxLength(4);                                // FUN_0057bdb0(4)
        Data->SetDrawFrame(true);                             // FUN_00579860(1)
        Data->SetFrameColor(
            reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba)); // FUN_00579870
        Data->SetAlignMode(2);                                // FUN_00579a60(2)
    }

    // 0x00562f0c..0x00562f1c.
    G_ServerFilterRowCounter = G_ServerFilterRowCounter + 1;
}

void ServerFilterDialog::ReadControls()
{
    // FUN_00562fd0. Toggle state 0 is "on" (property 0x2f reads it; SETZ).
    if (Target == nullptr)
        return;
    int State = 0;
    Rows[0].Check->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterPing = static_cast<unsigned char>(State == 0);
    Rows[1].Check->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterKnown = static_cast<unsigned char>(State == 0);
    Rows[2].Check->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterVersion = static_cast<unsigned char>(State == 0);
    Rows[3].Check->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterPassword = static_cast<unsigned char>(State == 0);
    Rows[4].Check->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterField3e = static_cast<unsigned char>(State == 0);

    // 0x005630ae..0x005630e0: the ping box's first 15 characters through _wtoi into the
    // maximum; the minimum back to 0.
    wchar_t Text[0x10];
    Rows[0].Data->GetText(Text, 0x10); // FUN_0057c2c0
    Target->PingMax = static_cast<unsigned>(MsvcrtWtoi(Text));
    Target->PingMin = 0;
}

void ServerFilterDialog::WriteControls()
{
    // FUN_005630f0. Property 0x30 with the enable byte (the toggle turns a zero into state 1).
    if (Target == nullptr)
        return;
    Rows[0].Check->SetProperty(0x30, Target->bFilterPing, 0);
    Rows[1].Check->SetProperty(0x30, Target->bFilterKnown, 0);
    Rows[2].Check->SetProperty(0x30, Target->bFilterVersion, 0);
    Rows[3].Check->SetProperty(0x30, Target->bFilterPassword, 0);
    Rows[4].Check->SetProperty(0x30, Target->bFilterField3e, 0);

    // 0x0056318f..0x005631b6: the maximum ping in decimal into the box, current attributes.
    wchar_t Text[0x10];
    _ultow(Target->PingMax, Text, 10);
    Rows[0].Data->SetText(Text, nullptr); // FUN_0057c0e0(text, 0)
}

void ServerFilterDialog::Close()
{
    ReleaseMouse(); // slot 33
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child); // FUN_0043d2a0
        Child = Next;
    }
    Destroy();   // slot 10
    delete this; // slot 0 (1)
}

bool ServerFilterDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_005631c0. Win3D's slot 9 (FUN_004b5060) with no data first.
    if (!Win3D::Create(NewParent, nullptr))
        return false;

    // 0x0056320c..0x00563216: in front at z = -1, the black backing plane.
    Offset[2] = -1.0f;
    SetMesh(kBackPlaneMesh, nullptr); // slot 38

    // 0x0056321c..0x0056329c: "Filter_BG" 0.01 behind the plane -- not kept.
    float const BackgroundOffset[3] = {Offset[0], Offset[1], Offset[2] + 0.00999999978f};
    float const ZeroHalfExtents[3] = {0.0f, 0.0f, 0.0f};
    float const ZeroVector338[3] = {0.0f, 0.0f, 0.0f};
    CreateStaticControl("Filter_BG", kBackgroundMesh, BackgroundOffset, nullptr, ZeroHalfExtents,
                        0, 0, 0, 1, 1, 0, 1, 0, 0, 0, ZeroVector338); // FUN_005a0db0

    // 0x005632a1..0x005632b2: the caller's filter, and a copy to put back on cancel.
    Target = static_cast<ServerFilter*>(Data);
    Backup = *Target; // FUN_00562930

    // 0x005632b7..0x00563406: the title -- IDS 0x60079 behind StyleNodes 0x9ca4 and 0x1df5, at
    // {-0.15, 0.19, 0} from this control, 0.3 x 0.04.
    Title = static_cast<RichTextWin*>(WinFactoryCreate("FilterTitle", "RichTextWin", this, nullptr));
    float const TitleOffset[3] = {Offset[0] + -0.150000006f, Offset[1] + 0.189999998f, Offset[2]};
    Title->SetAttachOffset(TitleOffset);              // FUN_00579c40
    Title->SetNormSize(0.300000012f, 0.0399999991f);  // FUN_00579cc0
    {
        RenderDisplayList Text;
        LoadIdsIntoRdl(0x60079, Text);                // FUN_0057da40, result ignored
        Title->ClearContent();                        // FUN_0057b370
        {
            StyleNode const Style(0x9ca4);
            Title->AppendNode(Style);                 // FUN_0057b470
        }
        {
            StyleNode const Style(0x1df5);
            Title->AppendNode(Style);
        }
        Title->AppendContent(Text);                   // FUN_0057b3e0
    }                                                 // list dtor inlined (FUN_0046c860 + free)

    // 0x0056340e..0x00563490: "FilterCancel" -- X.3db at {0.225, 0.175, -1}, half extents
    // {0.02, 0.02, 0}, tool tip 0x409.
    float const CancelOffset[3] = {0.224999994f, 0.174999997f, -1.0f};
    float const CancelHalfExtents[3] = {0.0199999996f, 0.0199999996f, 0.0f};
    CancelButton = CreateButtonControl("FilterCancel", kCancelMesh, nullptr, kSelectSound, nullptr,
                                       CancelOffset, nullptr, CancelHalfExtents, 0, 0, 0, 0,
                                       0x409, 0, 0); // FUN_005a0c90

    // 0x0056346c..0x005634d1: "FilterOk" -- a wire button at {0, -0.17, -1}, half extents
    // {0.05, 0.017, 0}, tool tip 0xbdb, caption IDS 0x411.
    float const OkOffset[3] = {0.0f, -0.170000002f, -1.0f};
    float const OkHalfExtents[3] = {0.0500000007f, 0.0170000009f, 0.0f};
    OkButton = CreateRichTextWireButtonControl("FilterOk", kSelectSound, OkOffset, OkHalfExtents,
                                               0, 0, 0xbdb, 0x411, 0, 0); // FUN_005a1950

    // 0x005634cb..0x005634f5: the five rows, DAT_00614828 up to DAT_006148c8.
    for (int Index = 0; Index < 5; ++Index)
        Rows[Index].Build(this, G_ServerFilterRows[Index]);

    WriteControls();  // FUN_005630f0
    CaptureMouse();   // slot 32
    if (Rows[0].Data != nullptr)
        Rows[0].Data->Activate(); // FUN_0057c750
    return true;
}

bool ServerFilterDialog::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005635a0. On a button-down (+4 == 2) the edit boxes are dropped one by one until the
    // mouse is over one of them or its label, which then takes the focus; rows without a box
    // are passed over. Every mouse event is taken.
    if (Event->Field1 == 2)
    {
        bool bFocused = false;
        for (int Index = 0; Index < 5; ++Index)
        {
            if (bFocused)
                continue;
            RichTextEditWin* const Data = Rows[Index].Data;
            if (Data == nullptr)
                continue;
            Data->Deactivate(false); // FUN_0057c7d0(0)
            float Ray[3];
            if (Data->HitTest(Win3DGetMouseRay(Ray)) ||
                Rows[Index].Label->HitTest(Win3DGetMouseRay(Ray)))
            {
                Data->Activate(); // FUN_0057c750
                bFocused = true;
            }
        }
    }
    return true;
}

bool ServerFilterDialog::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // FUN_00563530. Escape (without modifier bit 1) cancels: the copy back, then the close.
    if ((*Modifiers & 2) != 0 || Key != 0x1b)
        return false;
    *Target = Backup; // FUN_00562930
    Close();
    return true;
}

bool ServerFilterDialog::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_00563630.
    if (Msg == 0xf004)
    {
        // 0x00563642..0x00563663: the ping box finished -- dropped, then read.
        RichTextEditWin* const Data = Rows[0].Data;
        if (reinterpret_cast<uintptr_t>(Data) == static_cast<uintptr_t>(A))
        {
            if (Data != nullptr)
                Data->Deactivate(false); // FUN_0057c7d0(0)
            ReadControls();
            return true;
        }
    }
    else if (Msg == 0xf003)
    {
        // 0x0056366d..0x005636b6: cancel.
        *Target = Backup;
        Close();
        return true;
    }
    return WinControl::Slot23(Msg, A, B); // FUN_005a0840
}

intptr_t ServerFilterDialog::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_005636d0.
    if (Msg == 0x26)
    {
        if (A == reinterpret_cast<uintptr_t>(OkButton))
        {
            // 0x005636ed..0x0056372a: OK -- read the controls, close.
            ReadControls();
            Close();
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(CancelButton))
        {
            // 0x00563735..0x0056377d: cancel -- the copy back, close.
            *Target = Backup;
            Close();
            return 0;
        }
    }
    else if (Msg == 0x30)
    {
        // 0x00563785..0x00563790: a toggle changed.
        ReadControls();
        return 0;
    }
    return WinControl::SetProperty(Msg, A, B); // FUN_005a13c0
}

bool ServerFilterDialogRun(Win3D* Parent, ServerFilter* Filter)
{
    // FUN_005637b0.
    Win3D* const Created =
        WinFactoryCreate(kServerFilterDialogClass, kServerFilterDialogClass, Parent, Filter);
    if (Created == nullptr)
        return true;
    ServerFilterDialog* const Dialog = static_cast<ServerFilterDialog*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    Dialog->CaptureMouse(); // slot 32

    // 0x00563813..0x00563875: pump while a ServerFilterDialog is registered.
    int ExitCode = 0;
    while (Win3DFindByClassName(kServerFilterDialogClass) != nullptr)
    {
        if (!PumpMessages(&ExitCode))
            return false;
        double const Delta = GetClientFrameTimeCapped(); // FUN_0042d680
        Win3DUpdateAll(Delta);                           // FUN_0059e080
        CallClientStateUpdateSlot(static_cast<float>(Delta));
    }
    return true;
}
