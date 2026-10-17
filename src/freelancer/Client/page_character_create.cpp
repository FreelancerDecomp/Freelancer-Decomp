// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "page_character_create.h"

#include "chat_init.h"            // ChatGetBaseName (FUN_0046b890), ChatGetShipArchName (FUN_0046b6c0)
#include "engine_character.h"     // FEngineCharacterBody
#include "globals.h"              // GetClientFrameTimeCapped (FUN_0042d680)
#include "ids_rdl.h"              // RdlClear, LoadIdsIntoRdl (FUN_0057da40), RdlRemoveLastParagraph
#include "intro_page_util.h"
#include "login_mgr.h"            // G_LoginMgr (DAT_0067e7b8)
#include "menu_ui_init.h"         // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot (5645c0)
#include "nn_error.h"             // NnErrorRunModal (FUN_00473cb0)
#include "page_character_select.h" // G_CharacterCreateSelection (DAT_0067bcf8)
#include "space_camera.h"         // AsCamera, SpaceCameraSetHalfFovX (FUN_0040f8c0)
#include "space_scene_obj_draw.h" // SpaceCameraSetNearFar (FUN_0040fb60)
#include "tooltip_hover.h"        // FToolTipHoverInfo
#include "ui_rich_text_win.h"
#include "ui_text_edit.h"
#include "ui_text_msg_button.h"
#include "ui_win_deform.h"
#include "ui_win_object.h"
#include "win3d.h"                // Win3DGetCamera (FUN_0059da60), Win3DGetMouseRay (FUN_0059da70)
#include "win3d_immediate.h"      // Win3DVec3Add (FUN_00423b30)

#include "common_id.h"            // CreateID, ID_String
#include "common_newchar.h"       // NewChar::TheDB, FNewCharPackage, FNewCharPilot
#include "common_server_types.h"  // SCreateCharacterInfo
#include "common_text.h"
#include "common_universe.h"      // Archetype::GetShipID
#include "dacom.h"                // FDUMP

#include <stdint.h>               // uintptr_t
#include <string.h>
#include <wchar.h>                // wcscmp, wcscpy
#include <sstream>
#include <string>
#include <vector>

using namespace IntroPageUtil;

namespace
{
    // DAT_0067a890.
    PageCharacterCreate G_PageCharacterCreate(&G_CharacterCreateSelection.Info);

    // .rdata 0x005e1b1c -- the FDUMP source name.
    char const kCharCreateCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\CharCreate.cpp";

    // .rdata 0x005e1b7c.
    wchar_t const kHigeneko[] = L"higeneko";
    // .data 0x00614344 -- the same word in hiragana (U+3072 U+3052 U+306D U+3053).
    wchar_t const kHigenekoKana[] = L"\x3072\x3052\x306d\x3053";

    // .data 0x00614338 -- the characters the name box refuses. Create passes a count of 5, so
    // the terminator is in the set too.
    wchar_t const kNameFilter[] = L" \t\r\n";

    // .rdata 0x005cef90 -- the name box's prefix text.
    wchar_t const kSpace[] = L" ";

    // The eight caption loads of Create (0x00560423..0x005605eb): the list emptied (FUN_0046c860
    // over begin..end), the IDS text loaded (FUN_0057da40, its result not tested) and the
    // trailing paragraph cut (FUN_0057eb40) whatever the load did.
    void LoadIdsAlwaysCut(RenderDisplayList& Rdl, unsigned Ids)
    {
        RdlClear(Rdl);
        LoadIdsIntoRdl(Ids, Rdl);
        RdlRemoveLastParagraph(Rdl);
    }

    void AppendStyle(UiRichTextWin* Win, unsigned short Style)
    {
        StyleNode const Node(Style);
        Win->AppendNode(Node);
    }

    void AppendJustify(UiRichTextWin* Win, TextJustify Mode)
    {
        JustifyNode const Node(Mode);
        Win->AppendNode(Node);
    }

    void AppendRef(UiRichTextWin* Win, RenderDisplayList& Rdl)
    {
        RDLRefNode const Node(Rdl);
        Win->AppendNode(Node);
    }

    // ParagraphNode's vftable written into a stack slot and handed to FUN_0057b470.
    void AppendParagraph(UiRichTextWin* Win)
    {
        ParagraphNode const Node;
        Win->AppendNode(Node);
    }

    void SetButtonOffset(UiTextMsgButton* Button, float const* Offset)
    {
        // WinControl slot 42 (+0xa8) SetProperty(6, offset, 0).
        Button->SetProperty(6, reinterpret_cast<uintptr_t>(Offset), 0);
    }
}

PageCharacterCreate& GetPageCharacterCreate()
{
    return G_PageCharacterCreate;
}

// ---------------------------------------------------------------------------------------------

PageCharacterCreate::PageCharacterCreate(SCreateCharacterInfo* NewSelection)
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , Selection(NewSelection)
    , SelectionFactionIndex(&G_CharacterCreateSelection.FactionIndex)
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , DataFrameHome{0.0f, 0.0f, 0.0f}
    , DataFrame(nullptr)
    , BaseCostume(nullptr)
    , Back(nullptr)
    , SelectShip(nullptr)
    , CharacterName(nullptr)
    , StartingDesc(nullptr)
    , Title(nullptr)
    , NameLabel(nullptr)
{
    // FUN_0055fea0: FUN_00591ae0(FUN_00594580(+0x8c)), FUN_00591e30 on +0x8c, +0xb8 = the
    // argument, +0xbc = the argument + 0x40 (the pick's faction index, which is what
    // SelectionFactionIndex points at), the lists +0xc0 / +0xcc, +0x100..+0x11c = 0, the lists
    // +0x120..+0x174, +0xb4 = 0, the three vftables. +0xd8..+0xfc are not written; the static
    // object starts zeroed, which is what the initialisers above reproduce.
}

PageCharacterCreate::~PageCharacterCreate()
{
    // FUN_005600c0: the vftables back, the lists +0x174..+0x120 (FUN_0046ba90), +0x11c..+0x100
    // = 0, the lists +0xcc / +0xc0 (inlined), FUN_00591ef0 on +0x8c, FUN_00591ba0 -- the member
    // destructors in reverse declaration order, then the base.
}

int PageCharacterCreate::GetResult() const
{
    // FUN_005602a0.
    return Result;
}

bool PageCharacterCreate::Load(char const* FullPath)
{
    // FUN_005602b0: FUN_00592230 on +0x8c; true -> slot 9 (0, 0), MOV AL,1.
    if (!Desc.LoadFromFile(FullPath))
        return false;
    Create(nullptr, nullptr);
    return true;
}

bool PageCharacterCreate::HasResult() const
{
    // FUN_005602e0.
    return Result != 0;
}

void PageCharacterCreate::Destroy()
{
    // FUN_005602f0: FUN_005760f0(0xf) on the +0x80 face, then JMP FUN_00591de0.
    UnregisterEvent(0xf);
    UIPage::MasterWin::Destroy();
}

bool PageCharacterCreate::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_00560310. MasterWin::Create (FUN_00591cd0) with no payload; its result is not tested.
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0; // 0x0056033d

    // 0x0056033a..0x005603d9: the widgets through the IUIPage face (slot 4), in retail's order.
    DataFrame = static_cast<UiWinObject*>(FindWidget("DataFrame"));
    BaseCostume = static_cast<UiWinDeform*>(FindWidget("BaseCostume"));
    Back = static_cast<UiTextMsgButton*>(FindWidget("Back"));
    SelectShip = static_cast<UiTextMsgButton*>(FindWidget("SelectShip"));
    CharacterName = static_cast<UiTextEdit*>(FindWidget("CharacterName"));
    StartingDesc = static_cast<UiRichTextWin*>(FindWidget("StartingDesc"));
    Title = static_cast<UiRichTextWin*>(FindWidget("Title"));
    NameLabel = static_cast<UiRichTextWin*>(FindWidget("NameLabel"));

    RegisterEvent(0xf); // 0x005603df FUN_005760d0

    // 0x005603e4..0x0056041c: the ToolTip IDS of three controls (slot 46 +0xb8, the record's
    // +0x04).
    CharacterName->GetToolTipHoverInfo()->IdsAlt = 0xad8;
    Back->GetToolTipHoverInfo()->IdsAlt = 0xadc;
    SelectShip->GetToolTipHoverInfo()->IdsAlt = 0xadd;

    // 0x00560423..0x005605f0: the captions.
    LoadIdsAlwaysCut(BackText, 0x60029);
    LoadIdsAlwaysCut(Text60028, 0x60028);
    LoadIdsAlwaysCut(NameLabelText, 0x6006c);
    LoadIdsAlwaysCut(BaseLabelText, 0x6006b);
    LoadIdsAlwaysCut(ShipLabelText, 0x6006e);
    LoadIdsAlwaysCut(Text6006a, 0x6006a);
    LoadIdsAlwaysCut(MoneyLabelText, 0x60063);
    LoadIdsAlwaysCut(TitleText, 0x60034);

    // 0x005605f3..0x00560692: NameLabel (not emptied first) -- centred, style 0x9ca4, its caption.
    AppendJustify(NameLabel, TextJustifyCenter);
    AppendStyle(NameLabel, 0x9ca4);
    AppendRef(NameLabel, NameLabelText);

    // 0x00560694..0x00560705: Title emptied (FUN_0057b370), style 0x1df5, its caption.
    Title->ClearContent();
    AppendStyle(Title, 0x1df5);
    AppendRef(Title, TitleText);

    // 0x00560707..0x0056071d: the preview body's pose -- Z -6.0f (0xc0c00000), height -0.9f
    // (0xbf666666).
    BaseCostume->PoseZ = -6.0f;
    BaseCostume->PoseHeight = -0.9f;

    // 0x00560727..0x00560782: the preview camera's half horizontal fov scaled from the scene
    // camera's (DAT_0067dc90) by the ratio of the viewport widths (its own through slot 5 +0x14,
    // the scene's DAT_0067dcac - DAT_0067dca4): FILD width, FMUL fov, FIDIV scene width,
    // FSTP float -> FUN_0040f8c0. Then FUN_0040fb60(1.0f, 100.0f).
    FSpaceCamera* const Scene = Win3DGetCamera();
    int const SceneWidth = Scene->Viewport[2] - Scene->Viewport[0];
    FSpaceCamera* const Preview = &BaseCostume->Camera;
    int const* const PreviewRect = AsCamera(Preview)->GetViewport();
    int const PreviewWidth = PreviewRect[2] - PreviewRect[0];
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the scene's +0x98 is its angle on a 4:3 surface, so the width it spans
    // is the scene width over the Hor+ factor.
    SpaceCameraSetHalfFovX(
        Preview, static_cast<float>(static_cast<double>(PreviewWidth) *
                                    static_cast<double>(Scene->HalfFovXDeg) /
                                    (static_cast<double>(SceneWidth) / SpaceCameraHorPlusScale(Scene))));
#else
    SpaceCameraSetHalfFovX(
        Preview, static_cast<float>(static_cast<double>(PreviewWidth) *
                                    static_cast<double>(Scene->HalfFovXDeg) /
                                    static_cast<double>(SceneWidth)));
#endif
    SpaceCameraSetNearFar(Preview, 1.0f, 100.0f);

    // 0x00560787..0x00560809: Back's text list emptied (FUN_0046c860 on +0x4b4), a reference to
    // BackText cloned into it (FUN_0046c800, the first clone freed by FUN_0045eeb0), then
    // FUN_0059a110.
    RdlClear(Back->TextBodyList);
    {
        RDLRefNode const Node(BackText);
        AppendNodeClone(Back->TextBodyList, Node);
    }
    Back->ApplyCaptionLayout();

    // 0x0056080e..0x00560816: FUN_005799f0(1).
    StartingDesc->SetAutoScrollbar(true);

    // 0x0056081b..0x0056081f: FUN_00560c00(1).
    ShowSelection(true);

    // 0x00560824..0x00560856: the name box -- focus (FUN_0057c750), at most 0x17 characters
    // (FUN_0057bdb0), " \t\r\n" and its terminator refused (FUN_0057c860(.., 5)), a fixed style
    // (FUN_0057bdd0(1)).
    CharacterName->Activate();
    CharacterName->SetMaxLength(0x17);
    CharacterName->AddFilterChars(kNameFilter, 5);
    CharacterName->SetFixedStyle(true);

    // 0x0056085b..0x005608c2: StyleNode(0x9ca4) cloned into the fixed-style list (+0x4fc).
    {
        StyleNode const Node(0x9ca4);
        AppendNodeClone(CharacterName->FixedStyle, Node);
    }
    // 0x005608c8..0x0056092a: JustifyNode(1) cloned into the prefix list (+0x4dc).
    {
        JustifyNode const Node(TextJustifyCenter);
        AppendNodeClone(CharacterName->Prefix, Node);
    }
    // 0x00560930..0x00560996: TextNode(L" ", -1) cloned into the prefix list.
    {
        TextNode const Node(kSpace, -1);
        AppendNodeClone(CharacterName->Prefix, Node);
    }

    // 0x0056099c..0x005609cd: a frame in the chrome colour (FUN_00579860(1), FUN_00579870
    // (&DAT_00679bb4)), alignment 2 (FUN_00579a60), FUN_0057b520(0).
    CharacterName->SetDrawFrame(true);
    CharacterName->SetFrameColor(ChromeColor());
    CharacterName->SetAlignMode(2);
    CharacterName->SetLayoutFlag(false);

    // 0x005609d2..0x00560a0b: slide from -1.0 (0xbff00000), no leave, the frame's home.
    SlideT = -1.0;
    LeaveDirection = 0;
    bLeaving = 0;
    DataFrameHome[0] = DataFrame->Position[0];
    DataFrameHome[1] = DataFrame->Position[1];
    DataFrameHome[2] = DataFrame->Position[2];

    // 0x00560a0e..0x00560a3f: five controls hidden until the slide ends.
    HideWin3D(CharacterName);
    HideWin3D(BaseCostume);
    HideWin3D(NameLabel);
    HideWin3D(StartingDesc);
    HideWin3D(Title);

    // 0x00560a42..0x00560a57: FUN_00579a90(1) on both buttons.
    Back->SetSkipUpload(true);
    SelectShip->SetSkipUpload(true);
    return true;
}

void PageCharacterCreate::ShowSelection(bool bWithCostume)
{
    // FUN_00560c00 (RET 4).
    // 0x00560c26..0x00560c3e: the pick's name into the name box (FUN_0057bff0, FUN_0057c0e0
    // with no attributes).
    CharacterName->ClearText();
    CharacterName->SetText(Selection->Name, nullptr);

    // 0x00560c43..0x00560dea: StartingDesc emptied; the base caption and the home base's name
    // (FUN_0046b890 on the pick's +0x34).
    StartingDesc->ClearContent();
    AppendStyle(StartingDesc, 0x4433);
    AppendRef(StartingDesc, BaseLabelText);
    AppendParagraph(StartingDesc);
    AppendStyle(StartingDesc, 0x9ca4);
    AppendJustify(StartingDesc, TextJustifyCenter);
    AppendRef(StartingDesc, *ChatGetBaseName(Selection->Field_0x34));
    AppendParagraph(StartingDesc);
    AppendParagraph(StartingDesc);

    // 0x00560deb..0x00560e0e: NewChar::TheDB slot 1 (+0x04) with the pick's package.
    FNewCharPackage const* const Package = NewChar::TheDB->FindPackage(Selection->Field_0x38);
    if (Package != nullptr)
    {
        // 0x00560e14..0x005610b7: the money caption, then the package's money through a
        // basic_stringstream<unsigned short> (operator<< and ends, str() into a TextNode(.., -1)).
        AppendStyle(StartingDesc, 0x4433);
        AppendRef(StartingDesc, MoneyLabelText);
        AppendParagraph(StartingDesc);
        AppendStyle(StartingDesc, 0x9ca4);
        AppendJustify(StartingDesc, TextJustifyCenter);
        {
            std::wstringstream Money;
            Money << Package->Money << std::ends;
            {
                std::wstring const Text = Money.str();
                TextNode const Node(Text.c_str(), -1);
                StartingDesc->AppendNode(Node);
            }
            AppendParagraph(StartingDesc);
            AppendParagraph(StartingDesc);
        }

        // 0x0056108a..0x00561235: the ship caption and the ship's name (CreateID on the
        // package's ship string, Archetype::GetShipID, FUN_0046b6c0).
        AppendStyle(StartingDesc, 0x4433);
        AppendRef(StartingDesc, ShipLabelText);
        AppendParagraph(StartingDesc);
        AppendStyle(StartingDesc, 0x9ca4);
        AppendJustify(StartingDesc, TextJustifyCenter);
        ID_String ShipNickname;
        ShipNickname.Id = CreateID(Package->Ship.c_str());
        AppendRef(StartingDesc, *ChatGetShipArchName(Archetype::GetShipID(ShipNickname)));
        AppendParagraph(StartingDesc);
        AppendParagraph(StartingDesc);

        // 0x0056123a..0x00561293: the package description list emptied (FUN_0046d680 per node)
        // and loaded from strid_desc (the record's +0x08); a failed load ends the text here.
        RdlClear(PackageDescText);
        if (LoadIdsIntoRdl(static_cast<unsigned>(Package->StridDesc), PackageDescText))
        {
            // 0x00561299..0x0056133a: style 0x9ca4, left-justified, the description.
            AppendStyle(StartingDesc, 0x9ca4);
            AppendJustify(StartingDesc, TextJustifyLeft);
            AppendRef(StartingDesc, PackageDescText);
        }
    }

    // 0x00561340..0x00561349: the body only when asked.
    if (!bWithCostume)
        return;

    // 0x0056134f..0x0056136e: NewChar::TheDB slot 2 (+0x08) with the pick's pilot; none ->
    // FDUMP (0x100002, line 0x19d).
    FNewCharPilot const* const Pilot = NewChar::TheDB->FindPilot(Selection->Field_0x3c);
    if (Pilot == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kCharCreateCpp, 0x19d,
              "Failed to retrieve pilot information.\n");
        return;
    }

    // 0x00561374..0x00561387: the pilot's body costume (record +0x04, its c_str -- [EBP+8] or
    // _Nullstr) on BaseCostume (FUN_0059b020).
    BaseCostume->SetCostume(Pilot->Body.c_str());

    // 0x0056138c..0x005613c9: every body.anim script (record +0x14, 0x10-byte strings, each
    // through its c_str) started (FUN_00560ab0 with 0, 0, 1.0f, 2).
    FEngineCharacterBody& Body = BaseCostume->Body;
    for (std::vector<std::string>::const_iterator It = Pilot->BodyAnims.begin();
         It != Pilot->BodyAnims.end(); ++It)
    {
        Body.PlayAnimation(It->c_str(), 0, 0.0f, 1.0f, 2);
    }

    // 0x005613cb..0x005614a2: 0.1f (0x3dcccccd) of animation in steps of at most 0.2f
    // (DAT_005d043c; FCOMP, TEST AH,5 / JP -> 0.2f when Remaining >= 0.2f or unordered). The
    // step body is FUN_004cba60's (with a deformable: MixerWalk and IEngine slot 27 on the root
    // bone FUN_004ca480 with Step * AnimRate, the playback slots emptied when FUN_00560a80 finds
    // none running). Remaining -= Step (FST float), FCOMP double 0.0, TEST AH,0x41 / JZ -> loop
    // while the difference is > 0.
    float Remaining = 0.1f;
    for (;;)
    {
        float const Step = (Remaining < 0.2f) ? Remaining : 0.2f;
        Body.Advance(Step);
        double const Left = static_cast<double>(Remaining) - static_cast<double>(Step);
        Remaining = static_cast<float>(Left);
        if (!(Left > 0.0))
            break;
    }

    // 0x005614a8..0x005614e9: FUN_004c9a30's body (slot 9, the used slots emptied).
    Body.StopAnimations();

    // 0x005614eb..0x0056151a: the body.anim scripts started again.
    for (std::vector<std::string>::const_iterator It = Pilot->BodyAnims.begin();
         It != Pilot->BodyAnims.end(); ++It)
    {
        Body.PlayAnimation(It->c_str(), 0, 0.0f, 1.0f, 2);
    }
}

void PageCharacterCreate::ReadNameIntoSelection()
{
    // FUN_0057c2c0(pick, 0x18): the typed name into the pick.
    CharacterName->GetText(Selection->Name, 0x18);
    // wcscmp with L"higeneko": equal -> wcscpy the hiragana over it, and the box refilled
    // (FUN_0057bff0, FUN_0057c0e0(pick, 0)).
    if (wcscmp(Selection->Name, kHigeneko) == 0)
    {
        wcscpy(Selection->Name, kHigenekoKana);
        CharacterName->ClearText();
        CharacterName->SetText(Selection->Name, nullptr);
    }
}

void PageCharacterCreate::Confirm()
{
    // FUN_00561560. 0x00561563..0x005615c4.
    ReadNameIntoSelection();

    // 0x005615c6..0x005615d0: an empty name does nothing.
    if (Selection->Name[0] == L'\0')
        return;

    // 0x005615d2..0x005615e6: FUN_005aa750 on DAT_0067e7b8.
    int const Code = G_LoginMgr.CreateNewCharacter(*Selection);
    if (Code == 1)
    {
        // 0x00561614..0x00561628: created -- direction 1, then the leave (tail call).
        LeaveDirection = 1;
        bLeaving = 1;
        BeginLeave();
        return;
    }
    if (Code == 4)
    {
        // 0x00561601..0x0056160f: refused -- NN_Error with IDS 0x738, the page stays.
        NnErrorRunModal(0x738, false, nullptr);
        return;
    }
    // 0x005615e8..0x005615fc: anything else -- direction 2, then the leave (tail call).
    LeaveDirection = 2;
    bLeaving = 1;
    BeginLeave();
}

bool PageCharacterCreate::Slot23(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // LAB_00561630 (RET 0xc).
    bool bBack = false;
    bool bConfirm = false;
    if (Msg == 0x3004)
    {
        // 0x00561642..0x005616ad: a button click -- FUN_005645c0(0), the name read back.
        PlayOrRewindUiSoundSlot(0);
        ReadNameIntoSelection();
        // 0x005616af..0x005616c0: 0x300c Back, 0x300d SelectShip, anything else false.
        bBack = (A == 0x300c);
        bConfirm = (A == 0x300d);
    }
    else
    {
        // 0x005616c3..0x005616d0: 0xf003 back, 0xf004 confirm, anything else false.
        bBack = (Msg == 0xf003);
        bConfirm = (Msg == 0xf004);
    }

    if (bBack)
    {
        // 0x005616e0..0x005616f4: direction -1, FUN_00561b20, true.
        LeaveDirection = -1;
        BeginLeave();
        return true;
    }
    if (bConfirm)
    {
        // 0x005616d3..0x005616dd: FUN_00561560, true.
        Confirm();
        return true;
    }
    return false;
}

bool PageCharacterCreate::OnEvent(unsigned MsgId)
{
    // LAB_00561700 (RET 4, this = the +0x80 face): 0xf -> +0xe4 = -1 (the face's +0x64),
    // FUN_00561b20, true; anything else JMP FUN_0053f330 (XOR AL,AL / RET 4).
    if (MsgId == 0xf)
    {
        LeaveDirection = -1;
        BeginLeave();
        return true;
    }
    return false;
}

bool PageCharacterCreate::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00561730 (RET 4): a press (Field1 == 2) drops the name box's focus (FUN_0057c7d0(0))
    // and gives it back when the mouse ray (FUN_0059da70) hits the box (slot 4). Always false.
    if (Event->Field1 == 2)
    {
        CharacterName->Deactivate(false);
        float Ray[3];
        if (CharacterName->HitTest(Win3DGetMouseRay(Ray)))
            CharacterName->Activate();
    }
    return false;
}

void PageCharacterCreate::BeginLeave()
{
    // FUN_00561b20.
    bLeaving = 1;
    SlideT = 0.0;

    // 0x00561b3f..0x00561b6a: five controls hidden.
    HideWin3D(CharacterName);
    HideWin3D(BaseCostume);
    HideWin3D(NameLabel);
    HideWin3D(StartingDesc);
    HideWin3D(Title);

    // 0x00561b6d..0x00561b7f: both buttons stop taking clicks (+0x498).
    Back->bInteractive = 0;
    SelectShip->bInteractive = 0;

    // 0x00561b85..0x00561bbe: FUN_00579a90(1) on both buttons, FUN_00579890(0) on the name box
    // and both buttons.
    Back->SetSkipUpload(true);
    SelectShip->SetSkipUpload(true);
    CharacterName->SetToolTipEnabled(false);
    Back->SetToolTipEnabled(false);
    SelectShip->SetToolTipEnabled(false);

    // 0x00561bc3..0x00561bc5: FUN_00564570(0x1b).
    PlayUiSoundSlot(0x1b);
}

void PageCharacterCreate::Update()
{
    // LAB_00561790.
    // 0x00561797..0x005617cd: SelectShip's bit 0 follows whether the name box's text list
    // (+0x4e8) holds anything (RenderDisplayList::get_element_count), and its +0x498 copies it.
    unsigned char const OldFlags = SelectShip->Flags;
    unsigned char const bHasName = (CharacterName->TextDisplay.get_element_count() != 0) ? 1 : 0;
    SelectShip->Flags = static_cast<unsigned char>((OldFlags & 0xfe) | bHasName);
    SelectShip->bInteractive = static_cast<unsigned char>(SelectShip->Flags & 1);

    // 0x005617d3..0x005617f7: FCOMP 1.0 (DAT_005c89b8), TEST AH,0x41 / JP -> skip the slide-in
    // when SlideT > 1.0 or unordered; the fall-through (<= 1.0) goes to the leave when +0xe0 is
    // set.
    if (SlideT <= 1.0 && bLeaving == 0)
    {
        // 0x005617fd..0x00561823: the frame time (FUN_0042d680); FCOM 0.5 (DAT_005c75e8),
        // TEST AH,5 / JP -> no advance when >= 0.5 or unordered; below 0.5 SlideT += Dt * 1.5
        // (DAT_005d3c48).
        double const Dt = GetClientFrameTimeCapped();
        if (Dt < 0.5)
            SlideT = Dt * 1.5 + SlideT;

        // 0x00561825..0x00561855: FCOMP 0.0, TEST AH,5 / JP -> arrived when SlideT >= 0.0 or
        // unordered; below 0.0 (fall-through) the controls slide.
        if (SlideT < 0.0)
        {
            // 0x0056185b..0x005618dd: Back by {SlideT, 0, 0}, SelectShip by {-SlideT, 0, 0}
            // (slot 42 property 6), DataFrame at home + {0, SlideT, 0} (FUN_00423b30, slot 5).
            float const BackOffset[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
            SetButtonOffset(Back, BackOffset);
            float const ShipOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            SetButtonOffset(SelectShip, ShipOffset);
            float const FrameOffset[3] = {0.0f, static_cast<float>(SlideT), 0.0f};
            float FramePosition[3];
            Win3DVec3Add(FramePosition, DataFrameHome, FrameOffset);
            DataFrame->SetPosition(FramePosition);
        }
        else
        {
            // 0x005618e2..0x00561993: both buttons back to {0, 0, 0}, the frame home, the five
            // controls shown, FUN_00579a90(0) on both buttons, FUN_00579890(1) on the name box
            // and both buttons, SlideT = 2.0 (0x40000000).
            float const Zero[3] = {0.0f, 0.0f, 0.0f};
            SetButtonOffset(Back, Zero);
            SetButtonOffset(SelectShip, Zero);
            DataFrame->SetPosition(DataFrameHome);
            ShowWin3D(CharacterName);
            ShowWin3D(BaseCostume);
            ShowWin3D(NameLabel);
            ShowWin3D(StartingDesc);
            ShowWin3D(Title);
            Back->SetSkipUpload(false);
            SelectShip->SetSkipUpload(false);
            CharacterName->SetToolTipEnabled(true);
            Back->SetToolTipEnabled(true);
            SelectShip->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        // 0x0056199d..0x005619ca: FCOMP -0.8 (DAT_005d5078), TEST AH,0x41 / JNZ -> nothing at or
        // below -0.8 (or unordered); FCOMP -0.1 (DAT_005d5070), TEST AH,5 / JP -> nothing at or
        // above -0.1; strictly between, FUN_00564570(0x1b).
        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b);
    }

    // 0x005619cd..0x005619e3: the leave runs while +0xe0 is set and a direction is chosen.
    if (bLeaving != 0 && LeaveDirection != 0)
    {
        // 0x005619e9..0x00561a0f: SlideT = SlideT - Dt * 1.5 (FSUBR), FST double.
        double const Dt = GetClientFrameTimeCapped();
        SlideT = SlideT - Dt * 1.5;
        float const Slide = static_cast<float>(SlideT);

        // 0x00561a02..0x00561a5a: Back by {SlideT, 0, 0}, SelectShip by {-SlideT, 0, 0}.
        float const BackOffset[3] = {Slide, 0.0f, 0.0f};
        SetButtonOffset(Back, BackOffset);
        float const ShipOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        SetButtonOffset(SelectShip, ShipOffset);

        // 0x00561a60..0x00561a8f: DataFrame at {home x, SlideT + home y, home z} (slot 5).
        float const FramePosition[3] = {DataFrameHome[0], Slide + DataFrameHome[1],
                                        DataFrameHome[2]};
        DataFrame->SetPosition(FramePosition);

        // 0x00561a92..0x00561aa3: FCOMP -0.9 (DAT_005e1b90), TEST AH,0x41 / JP -> still sliding
        // when SlideT > -0.9 or unordered; at or below -0.9 (fall-through) the leave ends.
        if (SlideT <= -0.9)
        {
            // 0x00561aa5..0x00561ae5: CMP 3 / JA: -1 -> state 8, other values above 3 -> none;
            // 3 -> NN_Error 0x738 then state 8; 1 -> state 8; 2 -> state 5. Then +0xe0 = 0.
            unsigned const Direction = static_cast<unsigned>(LeaveDirection);
            if (Direction > 3)
            {
                if (LeaveDirection == -1)
                    Result = 8;
            }
            else if (Direction == 3)
            {
                NnErrorRunModal(0x738, false, nullptr);
                Result = 8;
            }
            else if (Direction == 1)
            {
                Result = 8;
            }
            else if (Direction == 2)
            {
                Result = 5;
            }
            bLeaving = 0;
        }
    }

    // 0x00561aec..0x00561b0c: Win3D's slot 11 body inlined (per child: bit0 ? slot 11 : 12).
    Win3D::Update();
}

void PageCharacterCreate::Draw()
{
    // FUN_00566500: JMP FUN_0059e5c0.
    Win3D::Draw();
}
