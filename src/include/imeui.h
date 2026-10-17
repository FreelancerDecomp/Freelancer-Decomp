// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// imeui.dll public surface вЂ” IMEFocus crosses into freelancer.exe.
// Declared once here; src/imeui/IMEFocus.h is a thin redirect.
//
// Retail layout (imeui.dll ctor 067112c0 + client FUN_00564060):
//   +0x00 vtable
//   +0x04 byte copied from constructor HWND; padding through +0x07
//   +0x08 list dummy node* (intrusive, node+0x08 = IMEInputBox*)
//   +0x0c box count
//   +0x10 active IMEInputBox*
//   +0x14..+0x17 flags (available @ +0x17)
//   +0x18 ConfirmPlayerText FontString* вЂ” CLIENT ONLY (alloc 0x1c)
// Base sizeof = 0x18; client wrapper sizeof = 0x1c.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#if defined(IMEUI_EXPORTS)
#define IMEUI_API __declspec(dllexport)
#else
#define IMEUI_API __declspec(dllimport)
#endif

struct IMEInputBox;
class CImeUiFont_Base;
class CImeUiFont;

// ---------------------------------------------------------------------------------------------
// CCaret / CInputBox / IMEInputBox -- the input-box half of imeui.dll. freelancer.exe derives
// its own box from IMEInputBox (FLImeInputBox, vftable 0x005e2250), so the classes are public.
// The access specifiers are part of the retail exported names (`U` public virtual, `M`
// protected virtual, `I` protected). Full classes are exported by IMEUI_API.
//
// Retail vtables (EXE\Disasm\imeui.dll.txt 0x06717188 / 0x067171b4):
//   CInputBox    0 dtor, 1 OnSetFocus, 2 OnKillFocus, 3 Resize, 4 HandleKey, 5 CharFilter,
//                6 UpdatedText (folded with ImeShowToolbar's RET 4 body), 7 ToggleInsertMode,
//                8 MoveCaret, 9 CalcCaretPos (_purecall in CInputBox), 10 RecalcCaretPos
//   IMEInputBox  the same eleven, then 11 and 12 _purecall and 13 ImeShowToolbar.
// Slot 11 is called by IMEFocus::Draw (0x067113fb) on every registered box with the draw
// context; slot 12 by the IME font's draw (FUN_06712410, 0x0671242e) on its owner box with
// (x, y, colour, text). freelancer.exe's box overrides both (0x0053e4a0, 0x005643f0).

class IMEUI_API CCaret
{
public:
    CCaret(HWND hWnd = NULL);
    ~CCaret();

    CCaret& operator=(const CCaret& other);

    void Show(bool bShow);
    bool SetPos(int x, int y);
    bool UpdateCaret();
    void SetBlink(bool bBlink);

public:
    bool m_bShow;               // +0x00
    DWORD BlinkTimestamp;       // +0x04
    bool m_bCaretState;         // +0x08
    int m_nX;                   // +0x0C
    int m_nY;                   // +0x10
    bool m_bBlink;              // +0x14
};

class IMEUI_API CInputBox
{
public:
    CInputBox();
    CInputBox(const CInputBox& other);
    virtual ~CInputBox();

    CInputBox& operator=(const CInputBox& other);

    virtual void OnSetFocus();                                              // Slot 1
    virtual void OnKillFocus();                                             // Slot 2
    virtual void Resize(const RECT* pRect);                                 // Slot 3
    virtual bool HandleKey(UINT uKey);                                      // Slot 4
    virtual bool CharFilter(unsigned short ch) const;                       // Slot 5
    virtual void UpdatedText(const unsigned short* pText);                  // Slot 6

    CCaret* GetCaret();
    const RECT* GetRect();
    DWORD GetMarginX() const;
    DWORD GetMarginY() const;
    unsigned short* GetTitle();
    void SetTitle(unsigned short* pTitle);
    void SetMargins(DWORD dwMarginX, DWORD dwMarginY);
    void SetInputBufferSize(DWORD dwSize);
    bool HandleWideChar(unsigned short ch);
    bool HandleMultiByteChar(char ch);

protected:
    virtual void ToggleInsertMode();                                        // Slot 7
    virtual bool MoveCaret(bool bRight, bool bHeadTail);                    // Slot 8
    virtual bool CalcCaretPos(POINT* pPoint) = 0;                               // Slot 9
    virtual void RecalcCaretPos();                                          // Slot 10

    void AddChar(unsigned short ch);
    bool DeleteChar();
    void ResetCaret();
    bool MoveCaretLeft(bool bHeadTail);
    bool MoveCaretRight(bool bHeadTail);
    bool MoveCaretHead();
    bool MoveCaretTail();
    int GetFreeInputBufferSize() const;

public:
    CCaret m_Caret;             // +0x04 (0x18 bytes: +0x04 .. +0x1C)
    RECT m_rcBounding;          // +0x1C (16 bytes: +0x1C .. +0x2C)
    int m_nCaretPosX;           // +0x2C
    int m_nCaretPosY;           // +0x30
    unsigned short m_Title[32]; // +0x34 .. +0x74
    DWORD m_dwMarginX;          // +0x74
    DWORD m_dwMarginY;          // +0x78
    bool m_bHasFocus;           // +0x7C
    DWORD m_dwBufferSize;       // +0x80
    unsigned short* m_pBuffer;  // +0x84
    DWORD m_dwCaretPos;         // +0x88
    bool m_bInsertMode;         // +0x8C
};

struct IMEUI_API IMEInputBox : public CInputBox
{
public:
    IMEInputBox(const LOGFONTA* pLogFont = NULL);
    IMEInputBox(const IMEInputBox& other);
    virtual ~IMEInputBox();

    IMEInputBox& operator=(const IMEInputBox& other);

    virtual void OnSetFocus() override;                                     // Slot 1
    virtual void OnKillFocus() override;                                    // Slot 2
    virtual void Resize(const RECT* pRect) override;                        // Slot 3

    // Slot 11 (+0x2c) -- _purecall here; IMEFocus::Draw hands every box its draw context.
    virtual void Draw(void* pContext) = 0;
    // Slot 12 (+0x30) -- _purecall here; the IME font draws its text through its owner box.
    // (Not called DrawText: windows.h makes that a macro.)
    virtual void DrawString(int X, int Y, unsigned Color, const wchar_t* Text) = 0;

    HFONT GetHFont();
    const LOGFONTA* GetFont();
    bool IsImeOn() const;
    void SetFont(LOGFONTA* pLogFont);

protected:
    virtual void ImeShowToolbar(bool bShow);                                // Slot 13
    virtual void ToggleInsertMode() override;                               // Slot 7
    virtual bool CalcCaretPos(POINT* pPoint) override;                      // Slot 9
    void UpdateAppearance();

public:
    CImeUiFont* m_pFont;        // +0x90
};

// Retail font contract, tables 06717140 and 067171ec: five methods, no destructor slot.
class CImeUiFont_Base
{
public:
    virtual void SetHeight(int Height) = 0;
    virtual void SetColor(unsigned Color) = 0;
    virtual void SetPosition(int X, int Y) = 0;
    virtual void GetTextExtent(const wchar_t* Text, int* Width, int* Height) = 0;
    virtual void Draw(const wchar_t* Text) = 0;
};

struct _IMEUI_APPEARANCE
{
    unsigned SymbolOnColor;                 // +0x00, resource 1
    unsigned SymbolOffColor;                // +0x04, resource 2
    unsigned SymbolBackgroundColor;         // +0x08, resource 3
    BYTE SymbolHeight;                     // +0x0c, resource 4
    BYTE SymbolAlpha;                      // +0x0d, resource 6
    BYTE SymbolPosition;                   // +0x0e, resource 5
    BYTE Reserved0F;
    CImeUiFont_Base* SymbolFont;             // +0x10
    unsigned CandidateTextColor;            // +0x14, resource 7
    unsigned CandidateSelectedTextColor;    // +0x18, resource 8
    unsigned CandidateColor;               // +0x1c, resource 9 (candidate font color)
    unsigned CompositionInputColor;         // +0x20, resource 10
    unsigned CompositionTargetColor;        // +0x24, resource 11
    unsigned CompositionConvertedColor;     // +0x28, resource 12
    unsigned CompositionTargetConvertedColor; // +0x2c, resource 13
    unsigned CompositionErrorColor;         // +0x30, resource 14
    BYTE CompositionAlpha;                 // +0x34, resource 15
    BYTE Reserved35[3];
    unsigned CompositionColor;              // +0x38, resource 16 (composition font color)
};
static_assert(sizeof(_IMEUI_APPEARANCE) == 0x3c, "IME appearance field contract");
typedef struct _IMEUI_APPEARANCE IMEUI_APPEARANCE;

// Intrusive list node вЂ” retail operator_new(0xc) in IMEFocus ctor.
struct FIMEBoxNode
{
    FIMEBoxNode* Next; // +0x00
    FIMEBoxNode* Prev; // +0x04
    IMEInputBox* Box;  // +0x08
};

#pragma pack(push, 4)
struct IMEUI_API IMEFocus
{
public:
    IMEFocus(HWND hWnd = NULL);
    IMEFocus(const IMEFocus& other);
    virtual ~IMEFocus() noexcept(false);

    IMEFocus& operator=(const IMEFocus& other);

    // Client overwrites vtable PTR_FUN_005e2210:
    // +0x00 FUN_00564150 dtor, +0x04 LAB_00563e70 DrawRect, +0x08 LAB_00563cf0 DrawTextW.
    virtual void DrawRect(int X1, int Y1, int X2, int Y2, DWORD Color) = 0;
    // LAB_06711150 push order в†’ (TextW, Color, LayoutBuf, EntryCount).
    // Layout entries are 0x20 bytes (float X/Y + RGB at +0x10); Count>1 draws underlines.
    virtual void DrawTextW(const wchar_t* Text, unsigned Color, void* Layout, unsigned Count) = 0;

    virtual void OnSize(HWND hWnd, UINT uMsg, LPARAM lParam);
    virtual void OnSetFocus();
    virtual void OnKillFocus();
    virtual bool OnLButtonDown(HWND hWnd, UINT uMsg, LPARAM lParam);
    virtual bool OnLButtonUp(HWND hWnd, UINT uMsg, LPARAM lParam);
    virtual bool OnMouseMove(HWND hWnd, UINT uMsg, LPARAM lParam);
    virtual bool OnChar(UINT uChar);
    virtual bool OnKeyDown(UINT uKey);
    virtual void SetFocus(IMEInputBox* pInputBox, bool bForce = false);

    void Draw(void* pContext);
    const _IMEUI_APPEARANCE* GetDefaultAppearance();
    IMEInputBox* GetFocus() const;
    IMEInputBox* GetInputBox(int index) const;
    CImeUiFont_Base* GetSymbolFont();
    bool HasRevertedLevel3Drawing() const;
    bool IgnoreHotKey(const MSG* pMsg) const;
    bool IsImeAvailable() const;
    int NumInputBoxes() const;
    IMEInputBox* RegisterInputBox(IMEInputBox* pInputBox);
    IMEInputBox* RemoveInputBox(int index);
    IMEInputBox* RemoveInputBox(IMEInputBox* pInputBox);
    void RevertLevel3Drawing(bool bRevert);
    void SetSupportLevel(int nLevel);
    bool WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, int* pHandled);

    static IMEFocus* GetSingleton();

public:
    BYTE Field04;                    // +0x04: ctor copies HWND low byte, copy ctor copies byte
    FIMEBoxNode* m_ListHead;         // +0x08
    int m_NumBoxes;                  // +0x0c
    IMEInputBox* m_pActiveInputBox;  // +0x10
    bool m_bFlag14;                  // +0x14
    bool m_bRevertLevel3Drawing;     // +0x15
    bool m_bUiInitOk;                // +0x16
    bool m_bImeAvailable;            // +0x17
};
#pragma pack(pop)

static_assert(sizeof(void*) == 4, "IMEFocus is 32-bit");
static_assert(sizeof(IMEFocus) == 0x18, "retail IMEFocus base through +0x17");
