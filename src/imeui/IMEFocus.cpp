// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "IMEFocus.h"
#include <ctfutb.h>
#include <cstring>
#include <cwchar>

static IMEFocus* GIMEFocusSingleton = NULL;
// Separate symbol-font table 06717140: height/draw/position are retail no-ops.
class FSymbolFont : public CImeUiFont_Base
{
public:
    FSymbolFont() : Symbol{0}, Color(0xffffffffu) {}
    void SetHeight(int) override {}
    void SetColor(unsigned InColor) override { Color = InColor; }
    void SetPosition(int, int) override {}
    void GetTextExtent(const wchar_t*, int* Width, int* Height) override
    {
        *Width = 0;
        *Height = 0;
    }
    void Draw(const wchar_t*) override {}
    virtual void SetSymbol(const wchar_t* Text)
    {
        wcsncpy(Symbol, Text, 5);
        Symbol[4] = 0;
    }
    wchar_t Symbol[5];
    unsigned Color;
};
static FSymbolFont GSymbolFont; // DAT_06719390
static IMEUI_APPEARANCE GDefaultAppearance;
static bool GbDefaultAppearanceReady = false;
static wchar_t GResourceStrings[4][1024];
static int GResourceStringIndex = -1;

// Constructor unwind 06716cc0 destroys the completed list subobject via 06711a00.
struct FIMEListConstructionGuard
{
    explicit FIMEListConstructionGuard(IMEFocus* InOwner) : Owner(InOwner), bComplete(false) {}
    ~FIMEListConstructionGuard()
    {
        if (bComplete)
            return;
        FIMEBoxNode* Node = Owner->m_ListHead->Next;
        while (Node != Owner->m_ListHead)
        {
            FIMEBoxNode* Next = Node->Next;
            Node->Prev->Next = Next;
            Next->Prev = Node->Prev;
            delete Node;
            --Owner->m_NumBoxes;
            Node = Next;
        }
        delete Owner->m_ListHead;
        Owner->m_ListHead = nullptr;
        Owner->m_NumBoxes = 0;
    }
    IMEFocus* Owner;
    bool bComplete;
};

// FUN_067121e0: invalid hex characters repeat the preceding digit (ESI).
static unsigned ParseAppearanceNumber(const wchar_t* Text)
{
    unsigned Value = 0;
    if (Text == nullptr)
        return Value;
    if (Text[0] == L'0' && (Text[1] == L'x' || Text[1] == L'X'))
    {
        Text += 2;
        unsigned Digit = 0;
        while (*Text != 0)
        {
            wchar_t const Character = *Text++;
            if (Character >= L'0' && Character <= L'9')
                Digit = Character - L'0';
            else if (Character >= L'A' && Character <= L'F')
                Digit = Character - L'A' + 10;
            else if (Character >= L'a' && Character <= L'f')
                Digit = Character - L'a' + 10;
            Value = (Value << 4) + Digit;
        }
    }
    else
    {
        while (*Text >= L'0' && *Text <= L'9')
            Value = Value * 10u + static_cast<unsigned>(*Text++ - L'0');
    }
    return Value;
}

static const wchar_t* LoadAppearanceString(HMODULE Module, unsigned Id)
{
    if (Module == nullptr)
        return nullptr;
    if (++GResourceStringIndex == 4)
        GResourceStringIndex = 0;
    wchar_t* Text = GResourceStrings[GResourceStringIndex];
    if (LoadStringW(Module, Id, Text, 2048) == 0)
        wsprintfW(Text, L"%d Not defined", Id);
    return Text;
}

static void StaticDrawRect(int X1, int Y1, int X2, int Y2, DWORD Color)
{
    if (GIMEFocusSingleton)
        GIMEFocusSingleton->DrawRect(X1, Y1, X2, Y2, Color);
}

// ImeUiCore still emits (x,y,text,Color); bridge to retail DrawTextW arity.
// Count==1 skips the underline loop in LAB_00563cf0 — degenerate layout entry.
static void StaticDrawFan(void* Layout, unsigned Count)
{
    if (GIMEFocusSingleton != nullptr)
        GIMEFocusSingleton->DrawTextW(GSymbolFont.Symbol, GSymbolFont.Color, Layout, Count);
}

static void StaticSendChar(unsigned Character)
{
    if (GIMEFocusSingleton != nullptr)
        GIMEFocusSingleton->OnChar(Character & 0xffffu);
}

static void StaticSendKey(unsigned Key)
{
    if (GIMEFocusSingleton != nullptr)
        GIMEFocusSingleton->OnKeyDown(Key);
}

// FUN_06712310/06712380: suppress and restore the operating-system language bar.
// GUIDs are the raw bytes at retail 06717494 and 06717804.
static const CLSID GLangBarClass = {0xebb08c45,0x6c4a,0x4fdc,{0xae,0x53,0x4e,0xb8,0xc4,0xc7,0xdb,0x8e}};
static DWORD GLangBarStatus = 0;

static void HideLanguageBar()
{
    ITfLangBarMgr* Manager = nullptr;
    HRESULT const Result = CoCreateInstance(GLangBarClass, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(ITfLangBarMgr), reinterpret_cast<void**>(&Manager));
    if (SUCCEEDED(Result) && Manager != nullptr)
    {
        DWORD Status;
        if (SUCCEEDED(Manager->GetShowFloatingStatus(&Status)))
            GLangBarStatus = Status;
        if ((GLangBarStatus & 0x800u) == 0)
            Manager->ShowFloating(8);
        Manager->Release();
    }
}

static void RestoreLanguageBar()
{
    ITfLangBarMgr* Manager = nullptr;
    HRESULT const Result = CoCreateInstance(GLangBarClass, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(ITfLangBarMgr), reinterpret_cast<void**>(&Manager));
    if (SUCCEEDED(Result) && Manager != nullptr)
    {
        if (GLangBarStatus != 0)
            Manager->ShowFloating(GLangBarStatus);
        Manager->Release();
    }
}

static bool CheckAnyIME()
{
    UINT Count = GetKeyboardLayoutList(0, NULL);
    if (Count == 0)
        return false;
    HKL* Layouts = new HKL[Count];
    Count = GetKeyboardLayoutList(Count, Layouts);
    bool bHasIme = false;
    for (UINT Index = 0; Index < Count; ++Index)
    {
        if (ImmIsIME(Layouts[Index]))
        {
            bHasIme = true;
            break;
        }
    }
    delete[] Layouts;
    return bHasIme;
}

IMEFocus::IMEFocus(HWND Window)
{
    // Retail IMEFocus::IMEFocus @ 067112c0 — field order through +0x17.
    Field04 = static_cast<BYTE>(reinterpret_cast<UINT_PTR>(Window));

    FIMEBoxNode* Dummy = new FIMEBoxNode();
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    Dummy->Box = NULL;
    m_ListHead = Dummy;
    m_NumBoxes = 0;
    FIMEListConstructionGuard ConstructionGuard(this);
    m_pActiveInputBox = NULL;
    m_bFlag14 = false;
    m_bRevertLevel3Drawing = false;
    m_bUiInitOk = false;
    m_bImeAvailable = false;

    GDrawRect = StaticDrawRect;
    GDrawFan = StaticDrawFan;
    GSendChar = StaticSendChar;
    GSendKey = StaticSendKey;
    GIMEFocusSingleton = this;

    bool bInitOk = ImeUi_Initialize(Window, false);
    m_bImeAvailable = bInitOk;
    if (bInitOk)
        m_bImeAvailable = CheckAnyIME();

    ImeUi_SetSupportLevel(2);
    HideLanguageBar();

    m_bUiInitOk = IsWindowUnicode(Window) != FALSE;
    RECT Rc;
    GetClientRect(Window, &Rc);
    ImeUi_SetScreenSize(Rc.right, Rc.bottom);
    ConstructionGuard.bComplete = true;
}

IMEFocus::IMEFocus(const IMEFocus& Other)
{
    Field04 = Other.Field04;
    FIMEBoxNode* Dummy = new FIMEBoxNode();
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    Dummy->Box = NULL;
    m_ListHead = Dummy;
    m_NumBoxes = 0;
    for (FIMEBoxNode* Source = Other.m_ListHead->Next; Source != Other.m_ListHead; Source = Source->Next)
    {
        FIMEBoxNode* Node = new FIMEBoxNode{Dummy, Dummy->Prev, Source->Box};
        Dummy->Prev->Next = Node;
        Dummy->Prev = Node;
        ++m_NumBoxes;
    }
    m_pActiveInputBox = Other.m_pActiveInputBox;
    m_bFlag14 = Other.m_bFlag14;
    m_bRevertLevel3Drawing = Other.m_bRevertLevel3Drawing;
    m_bUiInitOk = Other.m_bUiInitOk;
    m_bImeAvailable = Other.m_bImeAvailable;

}

IMEFocus::~IMEFocus() noexcept(false)
{
    FIMEListConstructionGuard DestructionGuard(this);
    SetFocus(nullptr, false);
    FIMEBoxNode* Node = m_ListHead->Next;
    while (Node != m_ListHead)
    {
        FIMEBoxNode* Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        delete Node;
        --m_NumBoxes;
        Node = Next;
    }
    GIMEFocusSingleton = nullptr;
    RestoreLanguageBar();
    delete m_ListHead;
    m_ListHead = NULL;
    m_NumBoxes = 0;
    DestructionGuard.bComplete = true;
}

IMEFocus& IMEFocus::operator=(const IMEFocus& Other)
{
    if (this != &Other)
    {
        FIMEBoxNode* Node = m_ListHead->Next;
        FIMEBoxNode* Source = Other.m_ListHead->Next;
        while (Node != m_ListHead && Source != Other.m_ListHead)
        {
            Node->Box = Source->Box;
            Node = Node->Next;
            Source = Source->Next;
        }
        while (Node != m_ListHead)
        {
            FIMEBoxNode* Next = Node->Next;
            Node->Prev->Next = Next;
            Next->Prev = Node->Prev;
            delete Node;
            --m_NumBoxes;
            Node = Next;
        }
        while (Source != Other.m_ListHead)
        {
            FIMEBoxNode* Added = new FIMEBoxNode{m_ListHead, m_ListHead->Prev, Source->Box};
            m_ListHead->Prev->Next = Added;
            m_ListHead->Prev = Added;
            ++m_NumBoxes;
            Source = Source->Next;
        }
    }
    m_pActiveInputBox = Other.m_pActiveInputBox;
    m_bFlag14 = Other.m_bFlag14;
    m_bRevertLevel3Drawing = Other.m_bRevertLevel3Drawing;
    m_bUiInitOk = Other.m_bUiInitOk;
    m_bImeAvailable = Other.m_bImeAvailable;
    return *this;
}

void IMEFocus::OnSize(HWND Window, UINT Message, LPARAM MessageData)
{
    (void)Window;
    (void)Message;
    ImeUi_SetScreenSize(LOWORD(MessageData), HIWORD(MessageData));
}

void IMEFocus::OnSetFocus()
{
    if (m_pActiveInputBox)
        m_pActiveInputBox->OnSetFocus();
}

void IMEFocus::OnKillFocus()
{
    if (m_pActiveInputBox)
        m_pActiveInputBox->OnKillFocus();
}

bool IMEFocus::OnLButtonDown(HWND Window, UINT Message, LPARAM MessageData)
{
    (void)Window;
    (void)Message;
    (void)MessageData;
    return false;
}

bool IMEFocus::OnLButtonUp(HWND Window, UINT Message, LPARAM MessageData)
{
    (void)Window;
    (void)Message;
    (void)MessageData;
    return false;
}

bool IMEFocus::OnMouseMove(HWND Window, UINT Message, LPARAM MessageData)
{
    (void)Window;
    (void)Message;
    (void)MessageData;
    return false;
}

bool IMEFocus::OnChar(UINT Character)
{
    if (m_pActiveInputBox)
        return m_pActiveInputBox->HandleWideChar(static_cast<wchar_t>(Character));
    return false;
}

bool IMEFocus::OnKeyDown(UINT Key)
{
    if (m_pActiveInputBox)
        return m_pActiveInputBox->HandleKey(Key);
    return false;
}

void IMEFocus::SetFocus(IMEInputBox* InputBox, bool bForce)
{
    if (bForce || m_pActiveInputBox != InputBox)
    {
        if (m_pActiveInputBox)
            m_pActiveInputBox->OnKillFocus();
        m_pActiveInputBox = InputBox;
        if (m_pActiveInputBox)
            m_pActiveInputBox->OnSetFocus();
    }
}

void IMEFocus::Draw(void* Context)
{
    if (m_bRevertLevel3Drawing)
        ImeUi_Render(true, false);
    FIMEBoxNode* Node = m_ListHead->Next;
    while (Node != m_ListHead)
    {
        Node->Box->Draw(Context);
        Node = Node->Next;
    }
    ImeUi_Render(!m_bRevertLevel3Drawing, true);
}

const _IMEUI_APPEARANCE* IMEFocus::GetDefaultAppearance()
{
    if (!GbDefaultAppearanceReady)
    {
        memset(&GDefaultAppearance, 0, sizeof(GDefaultAppearance));
        GbDefaultAppearanceReady = true;
        HMODULE Module = LoadLibraryW(L"ImeUi.dll");
        if (Module == nullptr)
            Module = LoadLibraryA("ImeUi.dll");
        GDefaultAppearance.SymbolFont = &GSymbolFont;
        GDefaultAppearance.SymbolOnColor = ParseAppearanceNumber(LoadAppearanceString(Module, 1));
        GDefaultAppearance.SymbolOffColor = ParseAppearanceNumber(LoadAppearanceString(Module, 2));
        GDefaultAppearance.SymbolBackgroundColor = ParseAppearanceNumber(LoadAppearanceString(Module, 3));
        GDefaultAppearance.SymbolHeight = static_cast<BYTE>(ParseAppearanceNumber(LoadAppearanceString(Module, 4)));
        GDefaultAppearance.SymbolPosition = static_cast<BYTE>(ParseAppearanceNumber(LoadAppearanceString(Module, 5)));
        GDefaultAppearance.SymbolAlpha = static_cast<BYTE>(ParseAppearanceNumber(LoadAppearanceString(Module, 6)));
        GDefaultAppearance.CandidateTextColor = ParseAppearanceNumber(LoadAppearanceString(Module, 7));
        GDefaultAppearance.CandidateSelectedTextColor = ParseAppearanceNumber(LoadAppearanceString(Module, 8));
        GDefaultAppearance.CandidateColor = ParseAppearanceNumber(LoadAppearanceString(Module, 9));
        GDefaultAppearance.CompositionInputColor = ParseAppearanceNumber(LoadAppearanceString(Module, 10));
        GDefaultAppearance.CompositionTargetColor = ParseAppearanceNumber(LoadAppearanceString(Module, 11));
        GDefaultAppearance.CompositionConvertedColor = ParseAppearanceNumber(LoadAppearanceString(Module, 12));
        GDefaultAppearance.CompositionTargetConvertedColor = ParseAppearanceNumber(LoadAppearanceString(Module, 13));
        GDefaultAppearance.CompositionErrorColor = ParseAppearanceNumber(LoadAppearanceString(Module, 14));
        GDefaultAppearance.CompositionAlpha = static_cast<BYTE>(ParseAppearanceNumber(LoadAppearanceString(Module, 15)));
        GDefaultAppearance.CompositionColor = ParseAppearanceNumber(LoadAppearanceString(Module, 16));
    }
    return &GDefaultAppearance;
}

IMEInputBox* IMEFocus::GetFocus() const
{
    return m_pActiveInputBox;
}

IMEInputBox* IMEFocus::GetInputBox(int Index) const
{
    if (Index < 0)
        return NULL;
    FIMEBoxNode* Node = m_ListHead->Next;
    while (Node != m_ListHead)
    {
        if (--Index < 0)
            return Node->Box;
        Node = Node->Next;
    }
    return NULL;
}

CImeUiFont_Base* IMEFocus::GetSymbolFont()
{
    return &GSymbolFont;
}

bool IMEFocus::HasRevertedLevel3Drawing() const
{
    return m_bRevertLevel3Drawing;
}

bool IMEFocus::IgnoreHotKey(const MSG* Message) const
{
    return m_NumBoxes != 0 && ImeUi_IgnoreHotKey(Message);
}

bool IMEFocus::IsImeAvailable() const
{
    return m_bImeAvailable;
}

int IMEFocus::NumInputBoxes() const
{
    return m_NumBoxes;
}

IMEInputBox* IMEFocus::RegisterInputBox(IMEInputBox* InputBox)
{
    // Retail 06711480 — insert before list head (push_back on circular list).
    if (InputBox == NULL)
        return NULL;
    if (m_pActiveInputBox == NULL)
        SetFocus(InputBox, false);

    FIMEBoxNode* Head = m_ListHead;
    FIMEBoxNode* Prev = Head->Prev;
    FIMEBoxNode* Node = new FIMEBoxNode();
    Node->Next = Head;
    Node->Prev = Prev;
    Head->Prev = Node;
    Prev->Next = Node;
    Node->Box = InputBox;
    ++m_NumBoxes;
    return InputBox;
}

IMEInputBox* IMEFocus::RemoveInputBox(int Index)
{
    if (Index < 0)
        return NULL;
    FIMEBoxNode* Head = m_ListHead;
    FIMEBoxNode* Node = Head->Next;
    while (Node != Head)
    {
        IMEInputBox* Box = Node->Box;
        if (--Index < 0)
        {
            if (m_pActiveInputBox == Box)
            {
                if (Box != NULL)
                    Box->OnKillFocus();
                m_pActiveInputBox = NULL;
            }
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            delete Node;
            --m_NumBoxes;
            return Box;
        }
        Node = Node->Next;
    }
    return NULL;
}

IMEInputBox* IMEFocus::RemoveInputBox(IMEInputBox* InputBox)
{
    if (InputBox == NULL || m_ListHead == NULL)
        return NULL;
    FIMEBoxNode* Head = m_ListHead;
    FIMEBoxNode* Node = Head->Next;
    while (Node != Head)
    {
        if (Node->Box == InputBox)
        {
            if (m_pActiveInputBox == InputBox)
            {
                InputBox->OnKillFocus();
                m_pActiveInputBox = NULL;
            }
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            delete Node;
            --m_NumBoxes;
            return InputBox;
        }
        Node = Node->Next;
    }
    return NULL;
}

void IMEFocus::RevertLevel3Drawing(bool bRevert)
{
    m_bRevertLevel3Drawing = bRevert;
}

void IMEFocus::SetSupportLevel(int Level)
{
    ImeUi_SetSupportLevel(static_cast<DWORD>(Level));
}

bool IMEFocus::WndProc(HWND Window, UINT Message, WPARAM MessageParam, LPARAM MessageData, int* HandledResult)
{
    BOOL bHandled = FALSE;
    int Result = 0;
    if (m_NumBoxes != 0)
    {
        Result = ImeUi_ProcessMessage(Window, Message, MessageParam, MessageData, &bHandled);
        if (!bHandled)
            Result = 0;
        switch (Message)
        {
        case WM_SIZE:
            OnSize(Window, static_cast<UINT>(MessageParam), MessageData);
            break;
        case WM_ACTIVATE:
            ImeUi_SetActive(LOWORD(MessageParam) == 0);
            break;
        case WM_SETFOCUS:
            OnSetFocus();
            break;
        case WM_KILLFOCUS:
            OnKillFocus();
            break;
        }
    }
    *HandledResult = Result;
    return bHandled != FALSE;
}

IMEFocus* IMEFocus::GetSingleton()
{
    return GIMEFocusSingleton;
}
