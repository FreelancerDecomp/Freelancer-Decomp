// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "type_select.h"

#include "globals.h"
#include "ids_rdl.h"      // LoadIdsIntoRdl (FUN_0057da40), RdlRemoveLastParagraph (FUN_0057eb40)
#include "login_mgr.h"
#include "menu_ui_init.h"
#include "nn_prompt.h"    // FUN_004740f0 / FUN_00474260

#include "common_errorhandler.h" // ErrorHandler::log_last_error, SeverityWarning
#include "common_text.h"  // RenderDisplayList

#include <mmsystem.h>
#include <string.h>

namespace
{
    // DAT_0067c210 -- set once the multiplayer notice has been accepted (0x00566c53); read
    // only at 0x00566c25.
    unsigned char G_bMultiplayerNoticeAccepted = 0;

    // s_E:\FL\Scratch\Source\Client\Interface\Intro.cpp (0x005e2724) and the WARNING format
    // (0x005e27a0) every ErrorHandler::log_last_error call of Intro.cpp passes.
    char const kIntroSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";
    char G_IntroLastErrorFormat[] = "%s(%d) : WARNING:LastError:%s";

    // DAT_00614ae0 -- 01 in the PE image and never written (two reads, 0x00566576 and
    // 0x00566a61): the IsNetworkAlive call is always skipped.
    unsigned char const G_bSkipNetworkAliveProbe = 1;

    // DLLVERSIONINFO as the inlined DllGetVersion probe fills it (cbSize = 0x14).
    struct FDllVersionInfo
    {
        DWORD Size;
        DWORD Major;
        DWORD Minor;
        DWORD Build;
        DWORD PlatformId;
    };
    typedef HRESULT(CALLBACK* FDllGetVersionFn)(FDllVersionInfo* Info);

    // The click the Win3D input router builds for a primary button release
    // (FUN_0059d140; see FWin3DMouseEvent).
    float const kSlideX = -0.455000013f;  // 0xbee8f5c3
    float const kColumnTopY = 0.172499999f; // DAT_005e28c0
    float const kColumnStepY = 0.0822000017f; // DAT_005e28c4
    float const kColumnZ = -1.0f;           // DAT_005c7500

    // Button i's slide position for the current AnimTime (0x00566612..0x005666d9, the same
    // body again at 0x00566772..0x00566837 for the slide-out).
    void PlaceColumnButton(TypeSelect* Self, unsigned Index)
    {
        int const Count = 6 - static_cast<int>(Index);
        double const Offset = static_cast<double>(Count) * 0.1; // DAT_005c7720
        double const SlideX = Offset + Self->AnimTime - 0.45500001311302185; // DAT_005e28d0
        float Position[3];
        Position[1] = static_cast<float>(static_cast<double>(kColumnTopY) -
                                         static_cast<double>(Index) *
                                             static_cast<double>(kColumnStepY));
        Position[2] = kColumnZ;
        float StaticOffset[3] = {0.0f, 0.0f, 0.0f};
        // FCOM DAT_005e28c8 (-0.455) / TEST AH,0x41 / JNZ: at or left of -0.455 (or unordered)
        // the button follows the slide and the mesh is pushed by Offset + AnimTime; right of
        // it the button rests at -0.455 and the mesh at its origin.
        if (SlideX > -0.45500001311302185)
        {
            Position[0] = kSlideX;
        }
        else
        {
            Position[0] = static_cast<float>(SlideX);
            StaticOffset[0] = static_cast<float>(Offset + Self->AnimTime);
        }
        Self->Buttons[Index].SetPosition(Position);   // slot 5
        Self->Statics[Index]->SetPosition(StaticOffset); // slot 5
    }

    TypeSelect G_TypeSelect; // DAT_0067b780
}

// ---------------------------------------------------------------------------------------
// TextMsgButton
// ---------------------------------------------------------------------------------------

TextMsgButton::TextMsgButton()
    : Command(0)
{
    // FUN_00566380: Win3D and TextWin parts come from the base constructors; then +0xec = 0,
    // vftable 0x005e2844, +0xe6 = 1.
    bUsePalette = 1;
}

TextMsgButton::~TextMsgButton()
{
    // FUN_0059ce40: JMP FUN_005a1af0 -- the TextWin destructor does the work.
}

void TextMsgButton::MouseEnter(float const* /*Ray*/)
{
    // FUN_005664e0.
    if (bUsePalette != 0)
        PlayUiRolloverSound(); // FUN_00564700
}

bool TextMsgButton::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005664a0.
    if (Event->Field1 != 2)
        return false;
    if (bUsePalette == 0)
        return false;
    if (Event->Field0 != -1)
        return false;
    if (Parent == nullptr)
        return false;
    return Parent->Slot23(0x3004, static_cast<unsigned>(Command), 0);
}

void TextMsgButton::Init(Win3D* NewParent, wchar_t const* Caption, float BoxWidth,
                         float BoxHeight, int NewCommand)
{
    // FUN_005753d0.
    Command = NewCommand;
    bUsePalette = 1;
    Create(NewParent, nullptr); // slot 9, result not checked
    Width = BoxWidth;
    Height = BoxHeight;
    SetText(Caption);             // slot 28
    SetFont("ButtonText", 1.0f);  // FUN_005a1cc0 (s_ButtonText 0x005d0410)
}

void TextMsgButton::SetFlagsE4E5(unsigned char A, unsigned char B)
{
    // FUN_005753b0.
    FlagE3 = 1;
    FieldE4 = A;
    FieldE5 = B;
}

// ---------------------------------------------------------------------------------------
// TypeSelect
// ---------------------------------------------------------------------------------------

TypeSelect::TypeSelect()
    : NextState(0)
    , Version(nullptr)
    , SensApi(nullptr)
    , IsNetworkAlive(nullptr)
    , PollIntervalMs(0)
    , LastPollMs(0)
    , Pad55c(0)
    , AnimTime(0.0)
    , PendingCommand(0)
    , bAnimating(0)
    , Phase(0)
{
    // FUN_005662a0: the inlined Win3D ctor, Event::Handler vftable 0x005cbed8 at +0x7c, the
    // five TextMsgButtons (`eh vector constructor iterator` FUN_005b7fba, ctor FUN_00566380,
    // dtor FUN_0059ce40), +0x550 = 0, +0x54c = 0, vftables 0x005e27cc / 0x005e27c4.
    // +0x80, +0x534..+0x548 and +0x554..+0x570 are not written by the retail ctor; the
    // instance is static storage (zero), which the member initialisers reproduce.
    Pad56d[0] = 0;
    Pad56d[1] = 0;
    Pad56d[2] = 0;
    for (unsigned Index = 0; Index < 5; ++Index)
        Statics[Index] = nullptr;
}

TypeSelect::~TypeSelect()
{
    // FUN_005661b0.
    if (SensApi != nullptr)
    {
        if (!FreeLibrary(SensApi))
            ErrorHandler::log_last_error(kIntroSourceFile, 0x17ed, SeverityWarning,
                                         G_IntroLastErrorFormat, GetLastError());
        SensApi = nullptr;
    }
    IsNetworkAlive = nullptr;
    // Then `eh vector destructor iterator` over the buttons (FUN_0059ce40 each), the
    // Event::Handler dtor FUN_00576010 on +0x7c and the inlined Win3D dtor -- the C++ member
    // and base destructors, in that order.
}

unsigned char TypeSelect::ProbeMultiplayerAvailable()
{
    // 0x00566564..0x0056659b: `CDPClient::GetClient(&DAT_0067e7bc)` (FUN_005b737a is the
    // DALib import thunk); none -> false. Otherwise, with DAT_00614ae0 set or no
    // IsNetworkAlive, true; else IsNetworkAlive(&flags) != 0. DAT_0067e7bc is the CDPClient
    // base of G_LoginMgr (DAT_0067e7b8, login_mgr.h).
    if (G_LoginMgr.GetClient() == nullptr)
        return 0;
    if (G_bSkipNetworkAliveProbe != 0 || IsNetworkAlive == nullptr)
        return 1;
    DWORD Flags = 0;
    return IsNetworkAlive(&Flags) != 0 ? 1 : 0;
}

bool TypeSelect::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_005668f0.
    AnimTime = 1.0; // +0x560/+0x564 = 0 / 0x3ff00000
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

    if (SensApi == nullptr)
    {
        // 0x00566939..0x005669dd: DllGetVersion of shdocvw.dll, inlined.
        DWORD Major;
        DWORD Minor;
        bool bHaveVersion = false;
        if (!IsBadWritePtr(&Major, 4) && !IsBadWritePtr(&Minor, 4))
        {
            Major = 0;
            Minor = 0;
            HMODULE const ShDocVw = LoadLibraryA("shdocvw.dll");
            if (ShDocVw != nullptr)
            {
                HRESULT Result;
                FDllGetVersionFn const GetVersion = reinterpret_cast<FDllGetVersionFn>(
                    GetProcAddress(ShDocVw, "DllGetVersion"));
                if (GetVersion != nullptr)
                {
                    FDllVersionInfo Info;
                    Info.Size = 0x14;
                    Info.Major = 0;
                    Info.Minor = 0;
                    Info.Build = 0;
                    Info.PlatformId = 0;
                    Result = GetVersion(&Info);
                    if (Result >= 0)
                    {
                        Major = Info.Major;
                        Minor = Info.Minor;
                    }
                }
                else
                {
                    Result = static_cast<HRESULT>(0x80004005L); // E_FAIL
                }
                FreeLibrary(ShDocVw);
                // 0x005669d5: only E_FAIL itself skips the version test.
                bHaveVersion = Result != static_cast<HRESULT>(0x80004005L);
            }
        }
        else
        {
            // 0x00566950 / 0x00566961: a bad pointer jumps straight to the version test with
            // whatever the locals held; they are stack slots the probe owns, never bad.
            Major = 0;
            bHaveVersion = true;
        }
        (void)Minor;

        // 0x005669df: version 5 or later -> sensapi.dll!IsNetworkAlive.
        if (bHaveVersion && Major >= 5)
        {
            SensApi = LoadLibraryA("sensapi.dll");
            if (SensApi != nullptr)
            {
                IsNetworkAlive = reinterpret_cast<FIsNetworkAliveFn>(
                    GetProcAddress(SensApi, "IsNetworkAlive"));
                if (IsNetworkAlive == nullptr)
                {
                    if (!FreeLibrary(SensApi))
                        ErrorHandler::log_last_error(kIntroSourceFile, 0x17d9, SeverityWarning,
                                                     G_IntroLastErrorFormat, GetLastError());
                    IsNetworkAlive = nullptr;
                    SensApi = nullptr;
                }
            }
            else
            {
                IsNetworkAlive = nullptr;
                SensApi = nullptr;
            }
        }
        else
        {
            // 0x00566a43.
            IsNetworkAlive = nullptr;
            SensApi = nullptr;
        }
    }

    // 0x00566a4f..0x00566aa7.
    Buttons[2].bUsePalette = ProbeMultiplayerAvailable(); // +0x34a
    PollIntervalMs = 1000;
    LastPollMs = timeGetTime();
    return true;
}

void TypeSelect::Destroy()
{
    // FUN_00566ab0.
    UnregisterAllEvents(); // FUN_00576190 on +0x7c
    if (SensApi != nullptr)
    {
        if (!FreeLibrary(SensApi))
            ErrorHandler::log_last_error(kIntroSourceFile, 0x17ed, SeverityWarning,
                                         G_IntroLastErrorFormat, GetLastError());
        SensApi = nullptr;
    }
    IsNetworkAlive = nullptr;
    Flags = static_cast<unsigned char>(Flags & 0xfc);

    // 0x00566b20..0x00566ba0: every child -- copy its class name (<= 31 chars) first, then
    // slot 10, then delete it through slot 0 if the name was "StaticControl" (s_ 0x005cb118,
    // a 14-byte compare). The next link is read before the calls.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        char ClassCopy[32];
        ClassCopy[0] = '\0';
        if (Child->ClassName != nullptr)
        {
            size_t Length = strlen(Child->ClassName);
            if (Length >= 0x20)
                Length = 0x1f;
            memcpy(ClassCopy, Child->ClassName, Length);
            ClassCopy[Length] = '\0';
        }
        Child->Destroy();
        if (memcmp(ClassCopy, "StaticControl", 14) == 0)
            delete Child;
        Child = Next;
    }

    // 0x00566ba7: the Version control goes through slot 0 (scalar deleting dtor) as well.
    if (Version != nullptr)
        delete Version;
    Version = nullptr;
    Win3DUnregister(this);
}

void TypeSelect::Update()
{
    // FUN_00566510.
    Win3D::Update(); // FUN_005a17b0 inlined, 0x0056651e..0x00566548

    unsigned const Now = timeGetTime();
    if (Now - LastPollMs > PollIntervalMs)
    {
        Buttons[2].bUsePalette = ProbeMultiplayerAvailable(); // +0x34a
        LastPollMs = Now;
    }

    // 0x005665a9: FCOMP 1.0 / TEST AH,0x41 / JP -- above 1.0 (or unordered) only a running
    // slide-out continues; otherwise a running slide-out continues and an idle column slides
    // in.
    bool const bSlideIn = (AnimTime <= 1.0) && bAnimating == 0;
    if (!(AnimTime <= 1.0) && bAnimating == 0)
        return;

    if (bSlideIn)
    {
        // 0x005665ce: dt capped at 0.5 (DAT_005c75e8), times 1.5 (DAT_005d3c48).
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        AnimTime = Dt * 1.5 + AnimTime;
        for (unsigned Index = 0; Index < 5; ++Index)
            PlaceColumnButton(this, Index);

        // 0x005666f9: the swish while -0.8 < AnimTime < -0.1 (DAT_005d5078 / DAT_005d5070).
        if (!(AnimTime > -0.80000000000000004))
            return;
        if (AnimTime < -0.10000000000000001)
            PlayUiSoundSlot(0x1b); // "ui_motion_swish"
        return;
    }

    // 0x00566741: slide-out, dt uncapped.
    AnimTime = AnimTime - GetClientFrameTimeCapped() * 1.5;
    for (unsigned Index = 0; Index < 5; ++Index)
        PlaceColumnButton(this, Index);

    // 0x0056685a: the swish while -0.4 < AnimTime < 0.0 (DAT_005e28b8 / DAT_005c8ba8).
    if (AnimTime < 0.0 && AnimTime > -0.40000000000000002)
        PlayUiSoundSlot(0x1b);

    // 0x0056688a: at or below -0.9 (DAT_005e1b90) the column is hidden; on the second such
    // frame the pending command becomes the next state.
    if (!(AnimTime <= -0.90000000000000002))
        return;
    Phase = Phase + 1;
    for (unsigned Index = 0; Index < 5; ++Index)
    {
        Buttons[Index].Flags = static_cast<unsigned char>(Buttons[Index].Flags & 0xfc);
        Statics[Index]->Flags = static_cast<unsigned char>(Statics[Index]->Flags & 0xfc);
    }
    if (Phase >= 2)
    {
        NextState = PendingCommand;
        bAnimating = 0;
    }
}

void TypeSelect::Draw()
{
    // FUN_00566500: JMP FUN_0059e5c0.
    Win3DDrawChildren(this);
}

bool TypeSelect::Slot23(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_00566bd0.
    if (Msg != 0x3004)
        return false;
    PlayOrRewindUiSoundSlot(0); // FUN_005645c0(0)

    // Byte table 0x00566fa8 over A - 3 (0..0xe), jump table 0x00566f94.
    switch (A)
    {
    case 3:    // Load Game
    case 0xa:  // Options
    case 0x10: // New Game
        // 0x00566efb: start the slide-out unless one is running.
        if (bAnimating != 0)
            return false;
        PendingCommand = static_cast<int>(A);
        Phase = 0;
        bAnimating = 1;
        AnimTime = 0.34999999999999998; // 0x3fd66666_66666666
        return true;

    case 4:
    {
        // 0x00566c25: Multiplayer. Until it has been accepted once (DAT_0067c210), the online-
        // play notice (misctextinfo2 0x601eb, style 0xc9b7, no server step, no tool tips);
        // declined -> 0x00566ee5: return true, nothing started, the latch left clear.
        if (G_bMultiplayerNoticeAccepted == 0)
        {
            if (!NnPromptRunYesNo(0x601eb, 0xc9b7, false, 0, 0)) // FUN_004740f0
                return true;
        }

        // 0x00566c4a..0x00566d31: the latch, then three lists, each loaded with FUN_0057da40
        // and, when that succeeds, trimmed by FUN_0057eb40: the Yes caption 0x601ed ("LAN"),
        // the No caption 0x601ec ("INTERNET") and the question 0x601f0.
        G_bMultiplayerNoticeAccepted = 1;
        RenderDisplayList LanCaption;      // [ESP+0x1c]
        RenderDisplayList InternetCaption; // [ESP+0x34]
        RenderDisplayList Question;        // [ESP+0x28]
        if (LoadIdsIntoRdl(0x601ed, LanCaption))
            RdlRemoveLastParagraph(LanCaption);
        if (LoadIdsIntoRdl(0x601ec, InternetCaption))
            RdlRemoveLastParagraph(InternetCaption);
        if (LoadIdsIntoRdl(0x601f0, Question))
            RdlRemoveLastParagraph(Question);

        // 0x00566d34..0x00566d60: FUN_00474260(&question, 0xc9b7, &lan, &internet, &choice, 0)
        // with the choice preset to -1; no choice -> 0x00566e31: return true.
        int Choice = -1;
        if (!NnPromptRunChoice(&Question, 0xc9b7, &LanCaption, &InternetCaption, &Choice,
                               false))
            return true;

        // 0x00566d66..0x00566d86
        G_bEnumerateLanHosts = 1;                      // DAT_00615b18
        G_bInternetServerList = 0;                     // DAT_00615b10
        if (Choice == 1)
            G_bInternetServerList = 1;
        G_LoginMgr.ResetServerLists();                 // FUN_005abc80(&DAT_0067e7b8)

        // 0x00566d8b..0x00566dc1: the slide-out of case 3 inlined; its outcome is the result.
        if (bAnimating != 0)
            return false;
        PendingCommand = static_cast<int>(A);
        Phase = 0;
        bAnimating = 1;
        AnimTime = 0.34999999999999998; // 0x3fd66666_66666666
        return true;
    }

    case 0xb:
        // 0x00566f40: FUN_00471f30(0) -- NN_Exit.
        RequestClientQuitDialog();
        return true;

    case 0x11:
        // 0x00566f5f.
        NextState = static_cast<int>(A);
        return true;

    default:
        return false;
    }
}

bool TypeSelect::OnEvent(unsigned MsgId)
{
    // FUN_00566fc0: USER_CANCEL (0xf) raises NN_Exit.
    if (MsgId != 0xf)
        return false;
    RequestClientQuitDialog(); // FUN_00471f30(0)
    return true;
}

TypeSelect& GetTypeSelect()
{
    return G_TypeSelect;
}
