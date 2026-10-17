// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ImeUiCore.h"
#include "CImeUiFont.h"
#include <imm.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <vector>
#include <msctf.h>
#include <ctfutb.h>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "version.lib")

// Global variables
HWND GWindow = NULL;
HWND GMessageWindow = NULL;
bool GbInitialized = false;
bool GbEnabled = false;
DWORD GSupportLevel = 0;
DWORD GRequestedSupportLevel = 0;
bool GbInsertMode = true;
bool GbShowReading = false;
bool GbCandidateListOpen = false;
bool GbVerticalCandidates = true;
static bool GbDisabled = false;
static DWORD GImeState = 0;
static int GScreenWidth = 0;
static int GScreenHeight = 0;
static CImeUiFont_Base* GCompositionFont = NULL;
static int GCandidateColor = 0;
static DWORD GCompositionColor = 0;
static DWORD GCompositionBackgrounds[5] = {};
static bool GbFinalizing = false;
static bool GbSyntheticKey = false;
static BYTE GCompositionAttributes[256] = {};
PFN_SENDCHAR GSendChar = NULL;
PFN_SENDKEY GSendKey = NULL;
static void SendComposition();
static bool GbUnicodeWindow = true;
static bool GbUnicodeIme = true;
static bool GbCapturedContext = false;
static OSVERSIONINFOA GVersion = { sizeof(OSVERSIONINFOA) };
static DWORD GCodePage = 0;
static BYTE GLeadBytes[256] = {};
static const wchar_t* GLanguageSymbol = L"A";
static bool GbHorizontalReading = false;
static DWORD GImeId[2] = {};
static HKL GImeIdLayout = NULL;
static DWORD GetImeId(unsigned Index);
static int ParseUnsigned(const char* Text);
static void SetupReadingApi();
static DWORD AllocateCandidates(HIMC Context, DWORD Index, CANDIDATELIST** Output);
static void FormatCandidate(wchar_t* Destination, int Number, const wchar_t* Source);
static LONG GetComposition(HIMC Context, DWORD Kind, void* Output, DWORD Capacity);
static void SendKey(unsigned Key, unsigned Count);
static void RemoveComposition(HWND Window, bool bAll, int NewLength);
static void ResetComposition();
static void UpdateReadingOrientation(DWORD Id);
static void AssembleReading();
DWORD GLanguage = 0;
HKL GKeyboardLayout = NULL;
HIMC GCapturedContext = NULL;
UINT GCaretBlinkInterval = 0;

POINT GPosition = { 0, 0 };
RECT GBounding = {};

wchar_t GComposition[MAX_COMPSTRING_SIZE] = { 0 };
wchar_t GCandidates[MAX_CANDLIST][MAX_COMPSTRING_SIZE] = { 0 };
wchar_t GReading[MAX_COMPSTRING_SIZE] = { 0 };

int GClientCaret = 0;
int GCompositionCursor = 0;
int GCandidateSelection = 0;
int GCandidateCount = 0;
int GCandidatePageSize = 0;
int GReadingSelection = 0;

_IMEUI_APPEARANCE GAppearance = { 0, 0x404040, 0xff000000, 24, 160, 0, 0, NULL, 0xffffffff, 0xff000000, 0, 0x00ffff00, 0xff, 0xff00, 0xff0000, 0xff0000, 128, {}, 0 };

static void UpdateCompositionColors()
{
    DWORD Alpha = GSupportLevel == 2 ? 0xff000000 : DWORD(GAppearance.CompositionAlpha) << 24;
    GCompositionBackgrounds[0] = GAppearance.CompositionInputColor | Alpha;
    GCompositionBackgrounds[1] = GAppearance.CompositionTargetColor | Alpha;
    GCompositionBackgrounds[2] = GAppearance.CompositionConvertedColor | Alpha;
    GCompositionBackgrounds[3] = GAppearance.CompositionTargetConvertedColor | Alpha;
    GCompositionBackgrounds[4] = GAppearance.CompositionErrorColor | Alpha;
}
PFN_DRAWRECT GDrawRect = NULL;
PFN_DRAWFAN GDrawFan = NULL;

typedef UINT (WINAPI *PFN_GETREADINGSTRING)(HIMC, UINT, PVOID, PINT, BOOL*, PUINT);
static PFN_GETREADINGSTRING GGetReadingString = NULL;

typedef BOOL (WINAPI *PFN_SHOWREADINGWINDOW)(HIMC, BOOL);
static PFN_SHOWREADINGWINDOW GShowReadingWindow = NULL;

typedef BOOL (WINAPI *PFN_IMMDISABLETEXTFRAMESERVICE)(DWORD);
static PFN_IMMDISABLETEXTFRAMESERVICE GImmDisableTextFrameService = NULL;
typedef void* (WINAPI *PFN_LOCKIMC)(HIMC);
typedef BOOL (WINAPI *PFN_UNLOCKIMC)(HIMC);
typedef void* (WINAPI *PFN_LOCKIMCC)(HIMCC);
typedef BOOL (WINAPI *PFN_UNLOCKIMCC)(HIMCC);
static PFN_LOCKIMC GLockImc = NULL;
static PFN_UNLOCKIMC GUnlockImc = NULL;
static PFN_LOCKIMCC GLockImcc = NULL;
static PFN_UNLOCKIMCC GUnlockImcc = NULL;

bool ImeUi_Initialize(HWND Window, bool bDisabled)
{
    GMessageWindow = Window;
    #pragma warning(suppress: 4996) // Retail platform branch uses GetVersionExA.
    GetVersionExA(&GVersion);
    GbUnicodeIme = GVersion.dwPlatformId == VER_PLATFORM_WIN32_NT || GVersion.dwMajorVersion > 4 || (GVersion.dwMajorVersion == 4 && GVersion.dwMinorVersion > 0);
    HMODULE Library = LoadLibraryA("imm32.dll");
    if (!Library)
    {
        GbDisabled = true;
        return false;
    }
    GLockImc = reinterpret_cast<PFN_LOCKIMC>(GetProcAddress(Library, "ImmLockIMC"));
    GUnlockImc = reinterpret_cast<PFN_UNLOCKIMC>(GetProcAddress(Library, "ImmUnlockIMC"));
    GLockImcc = reinterpret_cast<PFN_LOCKIMCC>(GetProcAddress(Library, "ImmLockIMCC"));
    GUnlockImcc = reinterpret_cast<PFN_UNLOCKIMCC>(GetProcAddress(Library, "ImmUnlockIMCC"));
    GImmDisableTextFrameService = reinterpret_cast<PFN_IMMDISABLETEXTFRAMESERVICE>(GetProcAddress(Library, "ImmDisableTextFrameService"));
    if (GImmDisableTextFrameService) GImmDisableTextFrameService(0xffffffff);
    GbUnicodeWindow = GVersion.dwPlatformId == VER_PLATFORM_WIN32_NT || IsWindowUnicode(Window);
    GbInitialized = true;
    if (!GbCapturedContext)
    {
        GbCapturedContext = true;
        ImeUi_SetMessageWindow(Window);
        GCapturedContext = ImmGetContext(Window);
        ImmReleaseContext(Window, GCapturedContext);
    }
    if (!GCapturedContext || bDisabled || !GDrawRect || !GDrawFan)
    {
        GbDisabled = true;
        ImeUi_SetState(false);
        GbInitialized = !GCapturedContext || bDisabled;
        return false;
    }
    GCaretBlinkInterval = GetCaretBlinkTime();
    GPosition.x = GPosition.y = 0;
    GCompositionFont = NULL;
    GCandidateColor = 0;
    GCompositionColor = 0;
    GBounding = { 0, 0, 640, 480 };
    ImeUi_CheckLanguage();
    ImeUi_SetSupportLevel(3);
    ImeUi_GetState();
    ImeUi_SetState(false);
    SetupReadingApi();
    return GbCapturedContext && GbInitialized && !GbDisabled;
}

void ImeUi_Uninitialize()
{
    ImeUi_SetState(false);
    GbInitialized = false;
    GWindow = NULL;
    GMessageWindow = NULL;
}

void ImeUi_SetState(bool bState)
{
    if (GbInitialized && GWindow)
    {
        if (GbDisabled)
            bState = false;
        if (GWindow == GMessageWindow)
        {
            HIMC hImc = bState ? GCapturedContext : NULL;
            ImmAssociateContext(GWindow, hImc);
        }
        GbEnabled = bState;
        if (bState)
        {
            ImeUi_GetState();
        }
    }
}

void ImeUi_SetMessageWindow(HWND Window)
{
    GWindow = Window;
}

bool ImeUi_IsCompositionEmpty()
{
    return !GbInitialized || GComposition[0] == 0;
}

bool ImeUi_IsSendingKey()
{
    return GbSyntheticKey;
}

DWORD ImeUi_GetState()
{
    if (!GbInitialized)
        return 0;
    ImeUi_CheckLanguage();
    HIMC Context = ImmGetContext(GWindow);
    if (!Context)
        return GImeState = 0;
    if (ImmIsIME(GKeyboardLayout))
    {
        if (PRIMARYLANGID(LOWORD(GLanguage)) == LANG_CHINESE)
        {
            DWORD Conversion;
            DWORD Sentence;
            ImmGetConversionStatus(Context, &Conversion, &Sentence);
            GImeState = (Conversion & 1) ? 1 : 2;
        }
        else
            GImeState = ImmGetOpenStatus(Context) ? 1 : 0;
    }
    else
        GImeState = 0;
    ImmReleaseContext(GWindow, Context);
    return GImeState;
}

void ImeUi_SetSupportLevel(DWORD Level)
{
    if (!GbInitialized)
        return;
    GRequestedSupportLevel = Level;
    if (Level >= 2 && Level <= 3)
    {
        if ((GLanguage & 0x3FF) == 0x12) // Korean
        {
            Level = 3;
        }
        GSupportLevel = Level;
        ImeUi_FinalizeString(true);
        UpdateCompositionColors();
    }
}

DWORD ImeUi_GetSupportLevel()
{
    return GSupportLevel;
}

void ImeUi_SetAppearance(const _IMEUI_APPEARANCE* Appearance)
{
    if (GbInitialized && Appearance)
    {
        memcpy(&GAppearance, Appearance, sizeof(_IMEUI_APPEARANCE));
        GAppearance.SymbolOnColor &= 0x00ffffff;
        GAppearance.SymbolOffColor &= 0x00ffffff;
        GAppearance.SymbolBackgroundColor &= 0x00ffffff;
        GAppearance.CompositionInputColor &= 0x00ffffff;
        GAppearance.CompositionTargetColor &= 0x00ffffff;
        GAppearance.CompositionConvertedColor &= 0x00ffffff;
        GAppearance.CompositionTargetConvertedColor &= 0x00ffffff;
        GAppearance.CompositionErrorColor &= 0x00ffffff;
        UpdateCompositionColors();
    }
}

void ImeUi_GetAppearance(_IMEUI_APPEARANCE* Appearance)
{
    if (Appearance)
    {
        memcpy(Appearance, &GAppearance, sizeof(_IMEUI_APPEARANCE));
    }
}

void ImeUi_SetInsertMode(bool bInsert)
{
    if (GbInitialized)
    {
        GbInsertMode = bInsert;
    }
}

bool ImeUi_GetInsertMode()
{
    return GbInsertMode;
}

bool ImeUi_IgnoreHotKey(const MSG* Message)
{
    if (GbInitialized && Message && (Message->message == WM_KEYDOWN || Message->message == WM_SYSKEYDOWN))
    {
        if (Message->wParam == VK_PROCESSKEY)
        {
            struct FHotKey { bool bControl; bool bShift; UINT Key; };
            static const FHotKey HotKeys[20] = {
                {false,false,VK_APPS}, {true,false,'8'}, {true,false,'Y'},
                {true,false,VK_DELETE}, {true,false,VK_F7}, {true,false,VK_F9},
                {true,false,VK_F10}, {true,false,VK_F11}, {true,false,VK_F12},
                {false,false,VK_F2}, {false,false,VK_F3}, {false,false,VK_F4},
                {false,false,VK_F5}, {false,false,VK_F10}, {false,true,VK_F6},
                {false,true,VK_F7}, {false,true,VK_F8}, {true,true,VK_F10},
                {true,false,VK_SPACE}, {true,false,VK_BACK}
            };
            UINT Key = ImmGetVirtualKey(Message->hwnd);
            bool bControl = GetKeyState(VK_CONTROL) < 0;
            bool bShift = GetKeyState(VK_SHIFT) < 0;
            for (int Index = 0; Index < 20; ++Index)
                if (HotKeys[Index].bControl == bControl && HotKeys[Index].bShift == bShift && HotKeys[Index].Key == Key)
                    return true;
        }
    }
    return false;
}

void ImeUi_SetCaretPosition(int X, int Y)
{
    if (GbInitialized)
    {
        GPosition.x = X;
        GPosition.y = Y;
    }
}

void ImeUi_SetFontAppearance(CImeUiFont_Base* Font, unsigned Color, const RECT* Rect)
{
    if (GbInitialized)
    {
        GCompositionFont = Font;
        GBounding = *Rect;
        GCandidateColor = GAppearance.CandidateColor ? GAppearance.CandidateColor : Color;
        GCompositionColor = GAppearance.CompositionColor ? GAppearance.CompositionColor : Color;
    }
}

void ImeUi_SetScreenSize(int Width, int Height)
{
    if (GbInitialized)
    {
        GScreenWidth = Width;
        GScreenHeight = Height;
    }
}

void ImeUi_FinalizeString(bool bSendMsg)
{
    if (GbInitialized && GWindow)
    {
        HIMC hImc = ImmGetContext(GWindow);
        if (hImc)
        {
            if (GbFinalizing)
            {
                ImmReleaseContext(GWindow, hImc);
                return;
            }
            GbFinalizing = true;
            if (GSupportLevel == 2 && bSendMsg)
            {
                SendComposition();
            }
            memset(GComposition, 0, sizeof(GComposition));
            memset(GCompositionAttributes, 0, sizeof(GCompositionAttributes));
            GClientCaret = 0;
            GCompositionCursor = 0;
            ImmNotifyIME(hImc, NI_CLOSECANDIDATE, 4, 0);
            ImmNotifyIME(hImc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(GWindow, hImc);
            GbFinalizing = false;
        }
    }
}

static void SendComposition()
{
    if (GSendChar)
    {
        for (int Index = 0; GComposition[Index]; ++Index)
            GSendChar(GComposition[Index]);
    }
    else if (GbUnicodeWindow)
    {
        for (int Index = 0; GComposition[Index]; ++Index)
            SendMessageW(GWindow, WM_CHAR, GComposition[Index], 0);
    }
    else
    {
        char Bytes[512];
        int Count = WideCharToMultiByte(ImeUi_GetCodePage(), 0, GComposition, -1, Bytes, sizeof(Bytes), NULL, NULL);
        for (int Index = 0; Index < Count; ++Index)
            SendMessageA(GWindow, WM_CHAR, static_cast<unsigned char>(Bytes[Index]), 0);
    }
}

void ImeUi_CheckLanguage()
{
    static HKL PreviousLayout = NULL;
    GKeyboardLayout = GetKeyboardLayout(0);
    GLanguage = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GKeyboardLayout));
    if (PreviousLayout == GKeyboardLayout) return;
    PreviousLayout = GKeyboardLayout;
    DWORD Language = PRIMARYLANGID(LOWORD(GLanguage));
    if (Language == LANG_CHINESE)
    {
        DWORD SubLanguage = SUBLANGID(LOWORD(GLanguage));
        GbVerticalCandidates = true;
        GLanguageSymbol = L"\x4e2d";
        if (SubLanguage != 1)
        {
            GLanguageSymbol = L"A";
            if (SubLanguage == 2)
            {
                GLanguageSymbol = L"\x4e2d";
                GbVerticalCandidates = GetImeId(0) == 0;
            }
        }
    }
    else if (Language == LANG_JAPANESE)
    {
        GbVerticalCandidates = true;
        GLanguageSymbol = L"\x3042";
    }
    else if (Language == LANG_KOREAN)
    {
        GbVerticalCandidates = false;
        GLanguageSymbol = L"\xac00";
    }
    else
        GLanguageSymbol = L"A";
    char CodePage[8];
    GetLocaleInfoA(LOWORD(GLanguage), LOCALE_IDEFAULTANSICODEPAGE, CodePage, sizeof(CodePage));
    GCodePage = ParseUnsigned(CodePage);
    for (int Index = 0; Index < 256; ++Index)
        GLeadBytes[Index] = static_cast<BYTE>(IsDBCSLeadByteEx(GCodePage, static_cast<BYTE>(Index)));
}

void ImeUi_UpdateCandidateList(HWND Window)
{
    HIMC Context = ImmGetContext(Window);
    if (!Context) return;
    CANDIDATELIST* List;
    GbShowReading = false;
    if (!AllocateCandidates(Context, 0, &List)) return;
    GCandidateSelection = static_cast<int>(List->dwSelection);
    GCandidateCount = static_cast<int>(List->dwCount);
    DWORD Start = 0;
    if (LOWORD(GLanguage) == 0x804 && GetImeId(0))
    {
        DWORD Accumulated = 0;
        DWORD Index = 0;
        for (; Index < List->dwCount; ++Index)
        {
            const wchar_t* Text = reinterpret_cast<const wchar_t*>(reinterpret_cast<BYTE*>(List) + List->dwOffset[Index]);
            DWORD Length = static_cast<DWORD>(wcslen(Text)) + 1;
            if (Accumulated + Length > 18)
            {
                if (Index > List->dwSelection) break;
                Start = Index;
                Accumulated = Length;
            }
            else Accumulated += Length;
        }
        GCandidatePageSize = static_cast<int>(Index - Start);
    }
    else
    {
        GCandidatePageSize = List->dwPageSize < 10 ? List->dwPageSize : 10;
        Start = (List->dwSelection / GCandidatePageSize) * GCandidatePageSize;
    }
    if (LOWORD(GLanguage) == 0x804 && !GetImeId(0))
        GCandidateSelection = -1;
    else
        GCandidateSelection -= Start;
    memset(GCandidates, 0, sizeof(GCandidates));
    DWORD DisplayIndex = 0;
    for (DWORD Index = Start; Index < List->dwCount && DisplayIndex < static_cast<DWORD>(GCandidatePageSize); ++Index)
    {
        const wchar_t* Text = reinterpret_cast<const wchar_t*>(reinterpret_cast<BYTE*>(List) + List->dwOffset[Index]);
        FormatCandidate(GCandidates[DisplayIndex], static_cast<int>(DisplayIndex) + 1, Text);
        ++DisplayIndex;
    }
    free(List);
    ImmReleaseContext(Window, Context);
    if (PRIMARYLANGID(LOWORD(GLanguage)) == LANG_KOREAN || (LOWORD(GLanguage) == 0x404 && !GetImeId(0)))
        GCandidateSelection = -1;
}

void ImeUi_GetReadingString(HWND Window)
{
    DWORD Id = GetImeId(0);
    if (!Id) return;
    HIMC Context = ImmGetContext(Window);
    if (!Context) return;
    unsigned Length = 0;
    unsigned Error = 0;
    const BYTE* Text = NULL;
    bool bUnicode = false;
    BYTE* LockedContext = NULL;
    BYTE* Private = NULL;
    std::vector<wchar_t> Reading;
    if (GGetReadingString)
    {
        BOOL bVertical;
        UINT MaxLength;
        Length = GGetReadingString(Context, 0, NULL, reinterpret_cast<int*>(&Error), &bVertical, &MaxLength);
        if (Length)
        {
            Reading.resize(Length);
            Length = GGetReadingString(Context, Length, Reading.data(), reinterpret_cast<int*>(&Error), &bVertical, &MaxLength);
            Text = reinterpret_cast<const BYTE*>(Reading.data());
        }
        bUnicode = true;
        GbHorizontalReading = !bVertical;
    }
    else
    {
        LockedContext = static_cast<BYTE*>(GLockImc(Context));
        HIMCC PrivateHandle = *reinterpret_cast<HIMCC*>(LockedContext + 0x124);
        BYTE* Record = NULL;
        switch (Id)
        {
        case 0x04020404:
        case 0x04030404:
        case 0x04040404:
            Private = static_cast<BYTE*>(GLockImcc(PrivateHandle));
            Record = *reinterpret_cast<BYTE**>(Private + 0x18);
            if (Record) { Length = *reinterpret_cast<unsigned*>(Record + 0x9c); Error = *reinterpret_cast<unsigned*>(Record + 0xa0); Text = Record + 0x38; bUnicode = true; }
            break;
        case 0x04010804:
        {
            unsigned Offset = GetImeId(1) < 2 ? 0x1c : 0x20;
            Private = static_cast<BYTE*>(GLockImcc(PrivateHandle));
            Record = *reinterpret_cast<BYTE**>(Private + Offset);
            if (Record)
            {
                Length = *reinterpret_cast<unsigned*>(Record + 0x9c);
                Error = *reinterpret_cast<unsigned*>(Record + 0xa0);
                if (Error >= Length) Error = Length;
                Text = Record + 0x38;
                bUnicode = true;
            }
            break;
        }
        case 0x04020804:
            Private = static_cast<BYTE*>(GLockImcc(PrivateHandle));
            Record = *reinterpret_cast<BYTE**>(Private + 0x20);
            if (Record)
            {
                bool bNt = GVersion.dwPlatformId == VER_PLATFORM_WIN32_NT;
                Length = *reinterpret_cast<unsigned*>(Record + (bNt ? 0x60 : 0x50));
                Error = *reinterpret_cast<unsigned*>(Record + (bNt ? 0x64 : 0x54));
                Text = Record + 0x40;
                bUnicode = bNt;
            }
            break;
        case 0x05000404:
            Private = static_cast<BYTE*>(GLockImcc(PrivateHandle));
            Record = *reinterpret_cast<BYTE**>(Private + 0x0c);
            if (Record) Record = *reinterpret_cast<BYTE**>(Record + 0x20);
            if (Record) { Length = *reinterpret_cast<unsigned*>(Record + 0x50); Error = *reinterpret_cast<unsigned*>(Record + 0x54); Text = Record + 0x40; }
            break;
        case 0x05010404:
        case 0x05020404:
        case 0x05030804:
            Private = static_cast<BYTE*>(GLockImcc(PrivateHandle));
            Record = *reinterpret_cast<BYTE**>(Private + 4);
            if (Record) Record = *reinterpret_cast<BYTE**>(Record + 0x18);
            if (Record) { Length = *reinterpret_cast<unsigned*>(Record + 0x60); Error = *reinterpret_cast<unsigned*>(Record + 0x64); Text = Record + 0x40; bUnicode = true; }
            break;
        }
        for (int Index = 0; Index < 4; ++Index) GCandidates[Index][0] = 0;
    }
    GCandidateSelection = -1;
    GCandidateCount = Length;
    unsigned DisplayIndex = 0;
    if (bUnicode)
    {
        for (; DisplayIndex < Length; ++DisplayIndex)
        {
            if (Error <= DisplayIndex && GCandidateSelection == -1) GCandidateSelection = DisplayIndex;
            GCandidates[DisplayIndex][0] = reinterpret_cast<const wchar_t*>(Text)[DisplayIndex];
            GCandidates[DisplayIndex][1] = 0;
        }
    }
    else
    {
        for (unsigned ByteIndex = 0; ByteIndex < Length; ++ByteIndex)
        {
            if (Error <= ByteIndex && GCandidateSelection == -1) GCandidateSelection = DisplayIndex;
            bool bLead = GLeadBytes[Text[ByteIndex]] != 0;
            MultiByteToWideChar(GCodePage, 0, reinterpret_cast<const char*>(Text + ByteIndex), bLead ? 2 : 1, GCandidates[DisplayIndex], 1);
            if (bLead) ++ByteIndex;
            ++DisplayIndex;
        }
        GCandidateCount = DisplayIndex;
    }
    GCandidates[DisplayIndex][0] = 0;
    if (!GGetReadingString)
    {
        GUnlockImcc(*reinterpret_cast<HIMCC*>(LockedContext + 0x124));
        GUnlockImc(Context);
        UpdateReadingOrientation(Id);
    }
    ImmReleaseContext(Window, Context);
    GbShowReading = true;
    if (GbHorizontalReading) AssembleReading();
    GCandidatePageSize = 10;
}

DWORD ImeUi_GetCodePage()
{
    return GCodePage;
}

static DWORD GLastCaretTick = 0;
static bool GbCaretVisible = false;
static wchar_t GRenderedComposition[256] = {};

static void DrawCompositionCaret(int X, int Y, int Height)
{
    if (GbCaretVisible) GDrawRect(X, Y + 1, X + 2, Y + Height - 1, GCompositionColor);
}

void ImeUi_Render(bool bDrawComposition, bool bShowToolbar)
{
    if (!GbInitialized || !GbEnabled || !GCompositionFont || (!bDrawComposition && !bShowToolbar)) return;
    if (GSupportLevel == 2 && !bShowToolbar) return;
    if (bShowToolbar) ImeUi_RenderToolbar(GAppearance, GBounding, GScreenWidth, GScreenHeight, GImeState, GLanguage, GLanguageSymbol);
    DWORD Tick = GetTickCount();
    if (Tick - GLastCaretTick > GCaretBlinkInterval)
    {
        GbCaretVisible = !GbCaretVisible;
        GLastCaretTick = Tick;
    }
    GCompositionFont->SetColor(GCompositionColor);
    int Length = static_cast<int>(wcslen(GComposition));
    int X = GPosition.x;
    int Y = GPosition.y;
    int SpaceWidth = 0;
    int FontHeight = 0;
    GCompositionFont->GetTextExtent(L" ", &SpaceWidth, &FontHeight);
    if (GSupportLevel == 3 && GClientCaret && GComposition[0])
    {
        wchar_t Saved = GComposition[GClientCaret];
        GComposition[GClientCaret] = 0;
        int Width;
        int Height;
        GCompositionFont->GetTextExtent(GComposition, &Width, &Height);
        X -= Width;
        GComposition[GClientCaret] = Saved;
    }
    int StartX = X;
    int AnchorX = 0;
    int AnchorY = 0;
    int RenderedLength = 0;
    int Index = 0;
    if (PRIMARYLANGID(LOWORD(GLanguage)) != LANG_KOREAN || GbCaretVisible)
    {
        for (; Index < Length; ++Index)
        {
            int PreviousX = X;
            wchar_t Saved = GComposition[Index + 1];
            GComposition[Index + 1] = 0;
            int Width;
            GCompositionFont->GetTextExtent(GComposition, &Width, &FontHeight);
            X = Width + StartX;
            GComposition[Index + 1] = Saved;
            DWORD Background = 0;
            bool bTarget = false;
            BYTE Attribute = GCompositionAttributes[Index];
            if (Attribute <= 4)
            {
                Background = GCompositionBackgrounds[Attribute];
                if (Attribute == ATTR_TARGET_CONVERTED && LOWORD(GetImeId(0)) != 0x804) bTarget = true;
            }
            if (GSupportLevel == 3)
            {
                if (bDrawComposition && PreviousX >= GBounding.left && PreviousX <= GBounding.right)
                    GDrawRect(PreviousX, Y, X, Y + FontHeight, Background);
            }
            else if (GSupportLevel == 2)
            {
                GDrawRect(PreviousX, Y, X, Y + FontHeight, Background);
                if (RenderedLength < 251) GRenderedComposition[RenderedLength++] = GComposition[Index];
                if (Index == GClientCaret) DrawCompositionCaret(PreviousX, Y, FontHeight);
            }
            if ((bTarget && !AnchorX) || (LOWORD(GetImeId(0)) == 0x804 && Index == GCompositionCursor))
            {
                AnchorX = PreviousX;
                AnchorY = Y;
            }
        }
    }
    if (GSupportLevel == 2)
    {
        if (Length && Index == GClientCaret) DrawCompositionCaret(X, Y, FontHeight);
        GRenderedComposition[RenderedLength] = 0;
        GRenderedComposition[RenderedLength + 1] = 0;
        int LineX = GPosition.x;
        int LineY = GPosition.y;
        wchar_t* Line = GRenderedComposition;
        while (*Line && Line - GRenderedComposition < 256)
        {
            GCompositionFont->SetPosition(LineX, LineY);
            GCompositionFont->Draw(Line);
            Line += wcslen(Line) + 1;
            LineY += FontHeight;
            LineX = GBounding.left;
        }
    }
    if (!bShowToolbar || !GCandidateCount || !GCandidates[0][0]) return;
    if (!AnchorX) { AnchorX = GPosition.x; AnchorY = GPosition.y; }
    int DigitWidth = 0;
    int MaxWidth = 0;
    int MaxHeight = 0;
    GCompositionFont->GetTextExtent(L"0", &DigitWidth, &MaxHeight);
    GCompositionFont->GetTextExtent(L" ", &SpaceWidth, &MaxHeight);
    int Padding = static_cast<unsigned>(SpaceWidth + 1) >> 1;
    int Widths[10];
    int TotalWidth = 0;
    if (GbShowReading && GbHorizontalReading)
        GCompositionFont->GetTextExtent(GReading, &MaxWidth, &MaxHeight);
    else
    {
        for (int CandidateIndex = 0; GCandidates[CandidateIndex][0] && CandidateIndex < GCandidatePageSize; ++CandidateIndex)
        {
            int Width;
            int Height;
            GCompositionFont->GetTextExtent(GCandidates[CandidateIndex], &Width, &Height);
            if (Width > MaxWidth) MaxWidth = Width;
            if (Height > MaxHeight) MaxHeight = Height;
            Widths[CandidateIndex] = Width;
            TotalWidth += Width;
        }
    }
    int Count = GbShowReading && GCandidateCount < GCandidatePageSize ? GCandidateCount : GCandidatePageSize;
    int OuterHeight = (GbVerticalCandidates && (!GbShowReading || !GbHorizontalReading) ? Count * MaxHeight : MaxHeight) + 2;
    if (static_cast<unsigned>(AnchorY + FontHeight + OuterHeight) > static_cast<unsigned>(GScreenHeight)) AnchorY -= OuterHeight;
    else AnchorY += FontHeight;
    if (AnchorY < 0) AnchorY = 0;
    int OuterWidth;
    if (!GbShowReading && LOWORD(GetImeId(0)) == 0x804) OuterWidth = TotalWidth + Count * Padding;
    else if (GbShowReading && GbHorizontalReading) OuterWidth = MaxWidth + Padding * 2 + 2;
    else if (!GbVerticalCandidates && !GbShowReading) OuterWidth = (MaxWidth + 1) * Count + 1;
    else OuterWidth = MaxWidth + Padding * 2 + 2;
    if (AnchorX < 0) AnchorX = 0;
    int Right = AnchorX + OuterWidth;
    int Bottom = AnchorY + OuterHeight;
    GDrawRect(AnchorX, AnchorY, Right, Bottom, GAppearance.CandidateSelectedTextColor);
    int InnerX = AnchorX + 1;
    int InnerY = AnchorY + 1;
    --Right;
    --Bottom;
    if (!GbShowReading && LOWORD(GetImeId(0)) != 0x804)
    {
        if (!GbVerticalCandidates)
        {
            int CellX = InnerX;
            for (int CandidateIndex = 0; CandidateIndex < Count; ++CandidateIndex)
            {
                GDrawRect(CellX, InnerY, CellX + MaxWidth, Bottom, GAppearance.CandidateTextColor);
                CellX += MaxWidth + 1;
            }
        }
        else
        {
            int Divider = InnerX + (static_cast<unsigned>(SpaceWidth) >> 1) + Padding + DigitWidth - 1;
            GDrawRect(InnerX, InnerY, Divider, Bottom, GAppearance.CandidateTextColor);
            GDrawRect(Divider + 1, InnerY, Right, Bottom, GAppearance.CandidateTextColor);
        }
    }
    else GDrawRect(InnerX, InnerY, Right, Bottom, GAppearance.CandidateTextColor);
    DWORD InverseColor = ~(GCandidateColor & 0xffffff) - 0x01000000;
    if (GbShowReading && GbHorizontalReading)
    {
        int TextX = InnerX + Padding;
        int LeftWidth = -1;
        if (GReadingSelection >= 0)
        {
            wchar_t Prefix[256];
            wcscpy(Prefix, GReading);
            Prefix[GReadingSelection + 1] = 0;
            int RightWidth;
            int Height;
            GCompositionFont->GetTextExtent(Prefix, &RightWidth, &Height);
            wchar_t Saved = Prefix[GReadingSelection];
            Prefix[GReadingSelection] = 0;
            GCompositionFont->GetTextExtent(Prefix, &LeftWidth, &Height);
            Prefix[GReadingSelection] = Saved;
            GDrawRect(TextX + LeftWidth, InnerY, TextX + RightWidth, InnerY + MaxHeight, GAppearance.CandidateSelectedTextColor);
        }
        GCompositionFont->SetPosition(TextX, InnerY);
        GCompositionFont->SetColor(GCandidateColor);
        GCompositionFont->Draw(GReading);
        if (LeftWidth >= 0)
        {
            GCompositionFont->SetPosition(TextX + LeftWidth, InnerY);
            GCompositionFont->SetColor(InverseColor);
            wchar_t Selected[256];
            wcscpy(Selected, GReading);
            Selected[GReadingSelection + 1] = 0;
            GCompositionFont->Draw(Selected + GReadingSelection);
        }
        return;
    }
    for (int CandidateIndex = 0; CandidateIndex < GCandidatePageSize; ++CandidateIndex)
    {
        if (CandidateIndex >= GCandidateCount) return;
        if (CandidateIndex == GCandidateSelection)
        {
            GCompositionFont->SetColor(InverseColor);
            if (!GbShowReading && !GbVerticalCandidates)
                GDrawRect(InnerX, InnerY, InnerX + Widths[CandidateIndex], InnerY + MaxHeight, GAppearance.CandidateSelectedTextColor);
            else
                GDrawRect(InnerX, InnerY + MaxHeight * CandidateIndex, InnerX + OuterWidth - 1, InnerY + MaxHeight * (CandidateIndex + 1), GAppearance.CandidateSelectedTextColor);
        }
        else GCompositionFont->SetColor(GCandidateColor);
        if (GCandidates[CandidateIndex][0])
        {
            if (!GbShowReading && !GbVerticalCandidates)
                GCompositionFont->SetPosition(InnerX + (static_cast<unsigned>(SpaceWidth) >> 1), InnerY);
            else GCompositionFont->SetPosition(InnerX + Padding, InnerY + MaxHeight * CandidateIndex);
            GCompositionFont->Draw(GCandidates[CandidateIndex]);
        }
        if (!GbShowReading && !GbVerticalCandidates)
            InnerX += LOWORD(GetImeId(0)) == 0x804 ? Widths[CandidateIndex] + Padding : MaxWidth + 1;
    }
}

int ImeUi_ProcessMessage(HWND Window, UINT MessageId, WPARAM MessageParam, LPARAM MessageData, BOOL* Handled)
{
    *Handled = FALSE;
    if (!GbInitialized || GbDisabled) return 0;
    switch (MessageId)
    {
    case WM_INPUTLANGCHANGE:
        {
            DWORD OldLanguage = PRIMARYLANGID(LOWORD(GLanguage));
            ImeUi_GetState();
            DWORD NewLanguage = PRIMARYLANGID(LOWORD(GLanguage));
            if (OldLanguage != NewLanguage)
            {
                DWORD Requested = GRequestedSupportLevel;
                ImeUi_SetSupportLevel(NewLanguage == LANG_KOREAN ? 3 : Requested);
                GRequestedSupportLevel = Requested;
            }
            SetupReadingApi();
            if (GShowReadingWindow)
            {
                HIMC Context = ImmGetContext(GWindow);
                if (Context)
                {
                    GShowReadingWindow(Context, FALSE);
                    ImmReleaseContext(GWindow, Context);
                }
            }
            return 0;
        }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        {
            static DWORD ModifierState[3] = { 0x80000000, 0x80000000, 0x80000000 };
            if (MessageId == WM_KEYUP || MessageId == WM_SYSKEYUP)
            {
                unsigned Modifier = 0;
                for (; Modifier < 3; ++Modifier)
                {
                    unsigned Key = Modifier == 0 ? VK_MENU : Modifier == 1 ? VK_CONTROL : VK_SHIFT;
                    if (!(ModifierState[Modifier] & 0x80000000) && MessageParam != Key && GetAsyncKeyState(Key) >= 0)
                    {
                        PostMessageA(GetFocus(), WM_KEYUP, Key, (ModifierState[Modifier] & 0x01ff0000) | 0xc0000001);
                        break;
                    }
                }
            }
            if (MessageParam == VK_MENU) ModifierState[0] = static_cast<DWORD>(MessageData);
            else if (MessageParam == VK_CONTROL) ModifierState[1] = static_cast<DWORD>(MessageData);
            else if (MessageParam == VK_SHIFT) ModifierState[2] = static_cast<DWORD>(MessageData);
            return 0;
        }
    case WM_IME_SETCONTEXT:
        return 0;
    case WM_IME_STARTCOMPOSITION:
        ResetComposition();
        *Handled = TRUE;
        return 0;
    case WM_IME_ENDCOMPOSITION:
        RemoveComposition(GWindow, true, 0);
        ResetComposition();
        return 0;
    case WM_IME_COMPOSITION:
        {
            *Handled = TRUE;
            HIMC Context = ImmGetContext(Window);
            if (!Context) return 0;
            wchar_t Text[256];
            if (MessageData & GCS_RESULTSTR)
            {
                DWORD Size = GetComposition(Context, GCS_RESULTSTR, Text, 256);
                Text[(Size & ~1u) / 2] = 0;
                int Length = static_cast<int>(wcslen(Text));
                RemoveComposition(GWindow, false, Length);
                wcscpy(GComposition, Text);
                SendComposition();
                ResetComposition();
            }
            if (MessageData & GCS_COMPSTR)
            {
                DWORD Size = GetComposition(Context, GCS_COMPSTR, Text, 256);
                Text[(Size & ~1u) / 2] = 0;
                int Length = static_cast<int>(wcslen(Text));
                RemoveComposition(GWindow, false, Length);
                wcscpy(GComposition, Text);
                LONG AttributeSize = GetComposition(Context, GCS_COMPATTR, GCompositionAttributes, 256);
                GCompositionAttributes[AttributeSize] = 0;
                if (LOWORD(GLanguage) == 0x404 && !GetImeId(0))
                {
                    if (!Length) GCandidateCount = 0;
                    else
                    {
                        GCandidateCount = 4;
                        GCandidateSelection = -1;
                        for (int Index = 3; Index >= 0; --Index)
                        {
                            GCandidates[Index][0] = Index < Length ? GComposition[Index] : 0;
                            if (Index < Length) GCandidates[Index][1] = 0;
                        }
                        memset(GComposition, 0, 8);
                        GCandidatePageSize = 10;
                        GbShowReading = true;
                        UpdateReadingOrientation(0);
                        if (GbHorizontalReading) AssembleReading();
                    }
                }
                GClientCaret = GetComposition(Context, GCS_CURSORPOS, NULL, 0);
                GCompositionCursor = GClientCaret;
                if (GSupportLevel == 3)
                {
                    SendComposition();
                    SendKey(VK_LEFT, static_cast<unsigned>(wcslen(GComposition + GClientCaret)));
                }
            }
            ImmReleaseContext(Window, Context);
            return 0;
        }
    case WM_IME_NOTIFY:
        switch (MessageParam)
        {
        case IMN_SETCONVERSIONMODE:
        case IMN_SETOPENSTATUS:
            ImeUi_GetState();
            return 0;
        case IMN_OPENCANDIDATE:
        case IMN_CHANGECANDIDATE:
            GbCandidateListOpen = true;
            *Handled = TRUE;
            ImeUi_UpdateCandidateList(Window);
            return 0;
        case IMN_CLOSECANDIDATE:
            GbCandidateListOpen = false;
            if (!GbShowReading)
            {
                GCandidateCount = 0;
                memset(GCandidates, 0, sizeof(GCandidates));
                *Handled = TRUE;
            }
            return 0;
        case IMN_PRIVATE:
            {
                if (!GbCandidateListOpen) ImeUi_GetReadingString(Window);
                DWORD Id = GetImeId(0);
                bool bHandled;
                switch (Id)
                {
                case 0x04010804:
                case 0x04020804:
                case 0x04020404:
                case 0x04030404:
                case 0x04040404:
                    bHandled = MessageData == 1 || MessageData == 2;
                    break;
                case 0x05000404:
                case 0x05010404:
                case 0x05020404:
                case 0x05030804:
                case 0x06000404:
                    bHandled = MessageData == 0x10 || MessageData == 0x11 || MessageData == 0x1a || MessageData == 0x1b || MessageData == 0x1c;
                    break;
                default:
                    return 0;
                }
                if (bHandled) *Handled = TRUE;
                return 0;
            }
        default:
            *Handled = TRUE;
            return 0;
        }
    default:
        return 0;
    }
}
static int ParseUnsigned(const char* Text)
{
    if (!Text) return 0;
    unsigned Result = 0;
    if (Text[0] == '0' && (Text[1] == 'X' || Text[1] == 'X'))
    {
        Text += 2;
        while (*Text)
        {
            unsigned Digit;
            if (*Text >= '0' && *Text <= '9') Digit = *Text - '0';
            else if (*Text >= 'A' && *Text <= 'F') Digit = *Text - 'A' + 10;
            else if (*Text >= 'a' && *Text <= 'f') Digit = *Text - 'a' + 10;
            else break;
            Result = Result * 16 + Digit;
            ++Text;
        }
    }
    else
    {
        while (*Text >= '0' && *Text <= '9')
            Result = Result * 10 + *Text++ - '0';
    }
    return static_cast<int>(Result);
}

static DWORD GetImeId(unsigned Index)
{
    if (Index > 1) return 0;
    if (GImeIdLayout == GKeyboardLayout) return GImeId[Index];
    GImeIdLayout = GKeyboardLayout;
    DWORD Layout = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GKeyboardLayout));
    WORD Language = LOWORD(Layout);
    if (Layout == 0xe0080404 || Layout == 0xe0090404 || Layout == 0xe00e0804)
    {
        char FileName[1024];
        if (ImmGetIMEFileNameA(GKeyboardLayout, FileName, 1023))
        {
            bool bRecognized = GGetReadingString != NULL;
            static const char* Names[] = { "TINTLGNT.IME", "CINTLGNT.IME", "MSTCIPHA.IME", "PINTLGNT.IME", "MSSCIPYA.IME" };
            for (int NameIndex = 0; NameIndex < 5 && !bRecognized; ++NameIndex)
                if (CompareStringA(0x409, NORM_IGNORECASE, FileName, -1, Names[NameIndex], -1) == CSTR_EQUAL)
                    bRecognized = true;
            if (bRecognized)
            {
                DWORD Handle;
                DWORD Size = GetFileVersionInfoSizeA(FileName, &Handle);
                if (Size)
                {
                    void* Data = malloc(Size);
                    if (Data && GetFileVersionInfoA(FileName, Handle, Size, Data))
                    {
                        VS_FIXEDFILEINFO* Version;
                        UINT VersionSize;
                        if (VerQueryValueA(Data, "\\", reinterpret_cast<void**>(&Version), &VersionSize))
                        {
                            DWORD Id = (((Version->dwFileVersionMS & 0xff) << 8) | (Version->dwFileVersionMS & 0xffff0000)) * 256;
                            bool bVersionRecognized = GGetReadingString != NULL;
                            if (Language == 0x404)
                                bVersionRecognized = bVersionRecognized || Id == 0x04020000 || Id == 0x04030000 || Id == 0x04040000 || Id == 0x05000000 || Id == 0x05010000 || Id == 0x05020000 || Id == 0x06000000;
                            else if (Language == 0x804)
                                bVersionRecognized = bVersionRecognized || Id == 0x04010000 || Id == 0x04020000 || Id == 0x05030000;
                            if (bVersionRecognized)
                            {
                                GImeId[0] = Id | Language;
                                GImeId[1] = Version->dwFileVersionLS;
                                free(Data);
                                // Retail returns first ID on the first lookup, irrespective of Index.
                                return GImeId[0];
                            }
                        }
                    }
                    free(Data);
                }
            }
        }
    }
    GImeId[0] = GImeId[1] = 0;
    return GImeId[Index];
}

static void SetupReadingApi()
{
    GGetReadingString = NULL;
    GShowReadingWindow = NULL;
    char FileName[264];
    if (ImmGetIMEFileNameA(GKeyboardLayout, FileName, 260))
    {
        HMODULE Library = LoadLibraryA(FileName);
        if (Library)
        {
            GGetReadingString = reinterpret_cast<PFN_GETREADINGSTRING>(GetProcAddress(Library, "GetReadingString"));
            GShowReadingWindow = reinterpret_cast<PFN_SHOWREADINGWINDOW>(GetProcAddress(Library, "ShowReadingWindow"));
        }
    }
}
// FUN_067142e0: synthesized cursor keys do not feed the client's normal key filtering.
static void SendKey(unsigned Key, unsigned Count)
{
    if (!Count) return;
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (GSendKey) GSendKey(Key);
        else
        {
            GbSyntheticKey = true;
            SendMessageA(GWindow, WM_KEYDOWN, Key, 0);
            GbSyntheticKey = false;
        }
    }
    if (!GSendKey)
    {
        GbSyntheticKey = true;
        SendMessageA(GWindow, WM_KEYUP, Key, 0);
        GbSyntheticKey = false;
    }
}

static void MoveCompositionCaretToEnd()
{
    SendKey(VK_RIGHT, static_cast<unsigned>(wcslen(GComposition) - GCompositionCursor));
}

// FUN_06714360(HWND,bool,int), including the HWND obscured by the decompiler.
static void RemoveComposition(HWND Window, bool bAll, int NewLength)
{
    if (GSupportLevel != 3) return;
    int Length = static_cast<int>(wcslen(GComposition));
    MoveCompositionCaretToEnd();
    if (bAll || GbInsertMode) NewLength = 0;
    if (NewLength < Length)
    {
        for (int Index = 0; Index < Length - NewLength; ++Index)
        {
            if (GSendKey) GSendKey(VK_BACK);
            else
            {
                GbSyntheticKey = true;
                SendMessageA(Window, WM_KEYDOWN, VK_BACK, 0);
                GbSyntheticKey = false;
            }
            if (!GSendKey) SendMessageA(Window, WM_CHAR, VK_BACK, 0);
        }
        if (!GSendKey)
        {
            GbSyntheticKey = true;
            SendMessageA(Window, WM_KEYUP, VK_BACK, 0);
            GbSyntheticKey = false;
            SendKey(VK_LEFT, NewLength);
            return;
        }
    }
    else NewLength = Length;
    SendKey(VK_LEFT, NewLength);
}

static void ResetComposition()
{
    memset(GComposition, 0, sizeof(GComposition));
    memset(GCompositionAttributes, 0, sizeof(GCompositionAttributes));
    GClientCaret = 0;
    GCompositionCursor = 0;
}

static void FormatCandidate(wchar_t* Destination, int Number, const wchar_t* Source)
{
    *Destination++ = static_cast<wchar_t>(Number % 10 + L'0');
    if (GbVerticalCandidates) *Destination++ = L' ';
    while (*Source) *Destination++ = *Source++;
    if (!GbVerticalCandidates) *Destination++ = L' ';
    *Destination = 0;
}
static DWORD AllocateCandidates(HIMC Context, DWORD Index, CANDIDATELIST** Output)
{
    DWORD Size = GbUnicodeIme ? ImmGetCandidateListW(Context, Index, NULL, 0) : ImmGetCandidateListA(Context, Index, NULL, 0);
    if (!Size) return 0;
    CANDIDATELIST* List = static_cast<CANDIDATELIST*>(malloc(Size));
    if (GbUnicodeIme)
    {
        *Output = List;
        return ImmGetCandidateListW(Context, Index, List, Size);
    }
    if (!List) return 0;
    DWORD Result = ImmGetCandidateListA(Context, Index, List, Size);
    if (!Result)
    {
        free(List);
        return 0;
    }
    DWORD UnicodeSize = List->dwOffset[0];
    for (DWORD CandidateIndex = 0; CandidateIndex < List->dwCount; ++CandidateIndex)
    {
        const char* Text = reinterpret_cast<const char*>(List) + List->dwOffset[CandidateIndex];
        UnicodeSize += MultiByteToWideChar(GCodePage, 0, Text, -1, NULL, 0) * 2;
    }
    CANDIDATELIST* Converted = static_cast<CANDIDATELIST*>(malloc(UnicodeSize));
    *Output = Converted;
    memcpy(Converted, List, List->dwOffset[0]);
    wchar_t* Next = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(Converted) + List->dwOffset[0]);
    for (DWORD CandidateIndex = 0; CandidateIndex < List->dwCount; ++CandidateIndex)
    {
        Converted->dwOffset[CandidateIndex] = static_cast<DWORD>(reinterpret_cast<BYTE*>(Next) - reinterpret_cast<BYTE*>(Converted));
        const char* Text = reinterpret_cast<const char*>(List) + List->dwOffset[CandidateIndex];
        Next += MultiByteToWideChar(GCodePage, 0, Text, -1, Next, 256);
    }
    free(List);
    return UnicodeSize;
}

static LONG GetComposition(HIMC Context, DWORD Kind, void* Output, DWORD Capacity)
{
    if (GbUnicodeIme) return ImmGetCompositionStringW(Context, Kind, Output, Capacity);
    char Bytes[512];
    LONG Result = ImmGetCompositionStringA(Context, Kind, Bytes, sizeof(Bytes));
    if (Kind == GCS_CURSORPOS) return static_cast<DWORD>(Result) >> 1;
    if (Kind == GCS_COMPATTR)
    {
        char Text[512];
        LONG TextResult = ImmGetCompositionStringA(Context, GCS_COMPSTR, Text, sizeof(Text));
        if (!TextResult) TextResult = ImmGetCompositionStringA(Context, GCS_RESULTSTR, Text, sizeof(Text));
        if (!TextResult) return Result;
        BYTE* Next = static_cast<BYTE*>(Output);
        for (unsigned Index = 0; Index < static_cast<unsigned>(Result); ++Index)
        {
            *Next++ = static_cast<BYTE>(Bytes[Index]);
            if (GLeadBytes[static_cast<BYTE>(Text[Index])]) ++Index;
        }
        return static_cast<LONG>(Next - static_cast<BYTE*>(Output));
    }
    if ((Kind == GCS_COMPSTR || Kind == GCS_RESULTSTR) && Result)
    {
        Bytes[Result] = 0;
        LONG Converted = MultiByteToWideChar(GCodePage, 0, Bytes, -1, static_cast<wchar_t*>(Output), Capacity);
        Result = Converted ? Converted * 2 - 2 : 0;
    }
    return Result;
}
static void UpdateReadingOrientation(DWORD Id)
{
    DWORD Layout = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GKeyboardLayout));
    if (Layout == 0xe00e0804 || Layout == 0xe0090404 || !Id)
    {
        GbHorizontalReading = true;
        return;
    }
    GbHorizontalReading = false;
    if (LOWORD(Id) != 0x404) return;
    DWORD Version = Id & 0xffff0000;
    char KeyName[260];
    lstrcpyA(KeyName, "software\\microsoft\\windows\\currentversion\\");
    lstrcatA(KeyName, Version < 0x05010000 ? "TINTLGNT" : "MSTCIPH");
    HKEY Key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, KeyName, 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        DWORD Mapping;
        DWORD Size = 4;
        DWORD Type;
        if (RegQueryValueExA(Key, "keyboard mapping", NULL, &Type, reinterpret_cast<BYTE*>(&Mapping), &Size) == ERROR_SUCCESS)
        {
            BYTE Value = static_cast<BYTE>(Mapping);
            if ((Version <= 0x05000000 && (Value == 0x22 || Value == 0x23)) || ((Version == 0x05010000 || Version == 0x05020000) && Value >= 0x22 && Value <= 0x24))
                GbHorizontalReading = true;
        }
        RegCloseKey(Key);
    }
}

static void AssembleReading()
{
    GReadingSelection = -1;
    GReading[0] = 0;
    for (int Index = 0; Index < GCandidateCount; ++Index)
    {
        if (GCandidateSelection == Index) GReadingSelection = static_cast<int>(wcslen(GReading));
        wcscat(GReading, GCandidates[Index]);
    }
}
void ImeUi_SetActive(bool bInactive)
{
    static bool bPreviousInactive = true;
    static int DefaultWindowSupported = -1;
    static DWORD SavedFloatingFlags = 0;
    bool bTransition = bPreviousInactive || bInactive;
    bPreviousInactive = bInactive;
    if (!bTransition) return;
    if (DefaultWindowSupported == -1)
        DefaultWindowSupported = GVersion.dwPlatformId == VER_PLATFORM_WIN32_NT && GVersion.dwMajorVersion >= 5 && (GVersion.dwMinorVersion > 1 || (GVersion.dwMinorVersion == 1 && lstrlenA(GVersion.szCSDVersion) != 0));
    HWND ImeWindow = ImmGetDefaultIMEWnd(GWindow);
    if (ImeWindow && bInactive && DefaultWindowSupported) SendMessageA(ImeWindow, WM_IME_CONTROL, 0x22, 0);
    if (SUCCEEDED(CoInitialize(NULL)))
    {
        static const CLSID LanguageBarClass = { 0xebb08c45, 0x6c4a, 0x4fdc, { 0xae, 0x53, 0x4e, 0xb8, 0xc4, 0xc7, 0xdb, 0x8e } };
        ITfLangBarMgr* Manager = NULL;
        if (SUCCEEDED(CoCreateInstance(LanguageBarClass, NULL, CLSCTX_INPROC_SERVER, __uuidof(ITfLangBarMgr), reinterpret_cast<void**>(&Manager))) && Manager)
        {
            if (!bInactive)
            {
                DWORD Flags;
                if (SUCCEEDED(Manager->GetShowFloatingStatus(&Flags))) SavedFloatingFlags = Flags;
                if (!(SavedFloatingFlags & 0x800)) Manager->ShowFloating(8);
            }
            else if (SavedFloatingFlags) Manager->ShowFloating(SavedFloatingFlags);
            Manager->Release();
        }
        CoUninitialize();
    }
    if (ImeWindow && !bInactive) SendMessageA(ImeWindow, WM_IME_CONTROL, 0x21, 0);
}
