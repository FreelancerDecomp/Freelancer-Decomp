// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "CInputBox.h"
#include "ImeUiCore.h"
#include <winnls.h>
#include <cwchar>

static char GDBCSLeadByte = 0;

CInputBox::CInputBox()
    : m_Caret(NULL)
{
    memset(&m_rcBounding, 0, sizeof(m_rcBounding));
    m_nCaretPosX = 0;
    m_nCaretPosY = 0;
    m_Title[0] = 0;
    m_dwMarginX = 0;
    m_dwMarginY = 0;
    m_bHasFocus = false;
    m_dwBufferSize = 0;
    m_pBuffer = NULL;
    m_dwCaretPos = 0;
    m_bInsertMode = true;
    SetMargins(1, 1);
    SetInputBufferSize(32);
}

CInputBox::CInputBox(const CInputBox& Other)
    : m_Caret(Other.m_Caret)
{
    m_rcBounding = Other.m_rcBounding;
    m_nCaretPosX = Other.m_nCaretPosX;
    m_nCaretPosY = Other.m_nCaretPosY;
    memcpy(m_Title, Other.m_Title, sizeof(m_Title));
    m_dwMarginX = Other.m_dwMarginX;
    m_dwMarginY = Other.m_dwMarginY;
    m_bHasFocus = Other.m_bHasFocus;
    m_dwBufferSize = Other.m_dwBufferSize;
    m_pBuffer = Other.m_pBuffer;
    m_dwCaretPos = Other.m_dwCaretPos;
    m_bInsertMode = Other.m_bInsertMode;
}

CInputBox::~CInputBox()
{
    delete[] m_pBuffer;
}

CInputBox& CInputBox::operator=(const CInputBox& Other)
{
    m_Caret = Other.m_Caret;
    m_rcBounding = Other.m_rcBounding;
    m_nCaretPosX = Other.m_nCaretPosX;
    m_nCaretPosY = Other.m_nCaretPosY;
    for (unsigned Index = 0; Index < 32; ++Index) m_Title[Index] = Other.m_Title[Index];
    m_dwMarginX = Other.m_dwMarginX;
    m_dwMarginY = Other.m_dwMarginY;
    m_bHasFocus = Other.m_bHasFocus;
    m_dwBufferSize = Other.m_dwBufferSize;
    m_pBuffer = Other.m_pBuffer;
    m_dwCaretPos = Other.m_dwCaretPos;
    m_bInsertMode = Other.m_bInsertMode;
    return *this;
}

void CInputBox::OnSetFocus()
{
    m_bHasFocus = true;
    ResetCaret();
    m_Caret.Show(true);
}

void CInputBox::OnKillFocus()
{
    m_bHasFocus = false;
    m_Caret.Show(false);
}

void CInputBox::Resize(const RECT* Rect)
{
    m_rcBounding = *Rect;
    RecalcCaretPos();
}

bool CInputBox::HandleKey(UINT Key)
{
    switch (Key)
    {
    case VK_BACK: return MoveCaret(false, false) ? DeleteChar() : false;
    case VK_DELETE: return DeleteChar();
    case VK_LEFT: return MoveCaret(false, false);
    case VK_RIGHT: return MoveCaret(true, false);
    case VK_HOME: return MoveCaret(false, true);
    case VK_END: return MoveCaret(true, true);
    case VK_INSERT: ToggleInsertMode(); return false;
    default: return false;
    }
}

bool CInputBox::CharFilter(unsigned short Character) const
{
    WORD CharacterType = 0;
    GetStringTypeExW(LOCALE_USER_DEFAULT, CT_CTYPE1, reinterpret_cast<const wchar_t*>(&Character), 1, &CharacterType);
    return (CharacterType & C1_CNTRL) == 0;
}

void CInputBox::UpdatedText(const unsigned short* pText)
{
    // Retail 0x06711110 is RET 4 (ICF-shared with ImeShowToolbar).
}

void CInputBox::ToggleInsertMode()
{
    m_bInsertMode = !m_bInsertMode;
}

bool CInputBox::MoveCaret(bool bRight, bool bHeadTail)
{
    DWORD Previous = m_dwCaretPos;
    if (bRight)
    {
        if (bHeadTail) m_dwCaretPos = static_cast<DWORD>(wcslen(reinterpret_cast<wchar_t*>(m_pBuffer)));
        else if (m_dwCaretPos < m_dwBufferSize && m_pBuffer[m_dwCaretPos] != 0) ++m_dwCaretPos;
    }
    else if (bHeadTail) m_dwCaretPos = 0;
    else
    {
        --m_dwCaretPos;
        if (static_cast<int>(m_dwCaretPos) < 0) m_dwCaretPos = 0;
    }
    bool bChanged = m_dwCaretPos != Previous;
    if (bChanged) RecalcCaretPos();
    return bChanged;
}

void CInputBox::RecalcCaretPos()
{
    POINT Position;
    if (CalcCaretPos(&Position)) m_nCaretPosX = Position.x;
    if (m_bHasFocus)
    {
        m_Caret.SetPos(m_rcBounding.left + m_nCaretPosX + m_dwMarginX,
                      m_rcBounding.top + m_nCaretPosY + m_dwMarginY);
    }
}

CCaret* CInputBox::GetCaret()
{
    return &m_Caret;
}

const RECT* CInputBox::GetRect()
{
    return &m_rcBounding;
}

DWORD CInputBox::GetMarginX() const
{
    return m_dwMarginX;
}

DWORD CInputBox::GetMarginY() const
{
    return m_dwMarginY;
}

unsigned short* CInputBox::GetTitle()
{
    return m_Title;
}

void CInputBox::SetTitle(unsigned short* Title)
{
    // Retail passes 0x3f to lstrcpynW despite a 32-WCHAR inline title.
    lstrcpynW(reinterpret_cast<wchar_t*>(m_Title), reinterpret_cast<wchar_t*>(Title), 0x3f);
}

void CInputBox::SetMargins(DWORD MarginX, DWORD MarginY)
{
    m_dwMarginX = MarginX;
    m_dwMarginY = MarginY;
}

void CInputBox::SetInputBufferSize(DWORD Size)
{
    if (Size == m_dwBufferSize) return;
    unsigned short* NewBuffer = new unsigned short[Size + 1];
    memset(NewBuffer, 0, (Size + 1) * sizeof(unsigned short));
    DWORD Count = Size < m_dwBufferSize ? Size : m_dwBufferSize;
    if (Count != 0)
    {
        wcsncpy(reinterpret_cast<wchar_t*>(NewBuffer), reinterpret_cast<wchar_t*>(m_pBuffer), Count);
        NewBuffer[Count] = 0;
    }
    if (m_dwCaretPos > m_dwBufferSize) m_dwCaretPos = m_dwBufferSize;
    delete[] m_pBuffer;
    m_pBuffer = NewBuffer;
    m_dwBufferSize = Size;
}

bool CInputBox::HandleWideChar(unsigned short Character)
{
    bool bAccepted = CharFilter(Character);
    if (bAccepted) AddChar(Character);
    return bAccepted;
}

bool CInputBox::HandleMultiByteChar(char Character)
{
    wchar_t WideCharacter;
    if (GDBCSLeadByte != 0)
    {
        char Bytes[2] = { GDBCSLeadByte, Character };
        GDBCSLeadByte = 0;
        MultiByteToWideChar(CP_ACP, 0, Bytes, 2, &WideCharacter, 1);
        wchar_t* ErrorText;
        FormatMessageW(0x1300, NULL, GetLastError(), 0x400, reinterpret_cast<wchar_t*>(&ErrorText), 0, NULL);
    }
    else
    {
        if (IsDBCSLeadByte(Character))
        {
            GDBCSLeadByte = Character;
            return false;
        }
        MultiByteToWideChar(CP_ACP, 0, &Character, 1, &WideCharacter, 1);
    }
    return HandleWideChar(static_cast<unsigned short>(WideCharacter));
}

void CInputBox::AddChar(unsigned short Character)
{
    if (!m_bInsertMode) DeleteChar();
    if (GetFreeInputBufferSize() == 0)
    {
        DeleteChar();
        if (GetFreeInputBufferSize() == 0)
        {
            ImeUi_FinalizeString(false);
            return;
        }
    }
    unsigned short* Tail = m_pBuffer + m_dwCaretPos;
    size_t Length = wcslen(reinterpret_cast<wchar_t*>(Tail));
    if (Length != 0) memmove(Tail + 1, Tail, Length * sizeof(unsigned short));
    *Tail = Character;
    ++m_dwCaretPos;
    RecalcCaretPos();
    UpdatedText(m_pBuffer);
}

bool CInputBox::DeleteChar()
{
    if (static_cast<int>(m_dwCaretPos) < 0)
    {
        m_dwCaretPos = 0;
        return false;
    }
    if (m_dwCaretPos >= m_dwBufferSize) m_dwCaretPos = m_dwBufferSize - 1;
    m_pBuffer[m_dwCaretPos] = 0;
    RecalcCaretPos();
    UpdatedText(m_pBuffer);
    return true;
}

void CInputBox::ResetCaret()
{
    RecalcCaretPos();
}

bool CInputBox::MoveCaretLeft(bool bHeadTail)
{
    return MoveCaret(false, bHeadTail);
}

bool CInputBox::MoveCaretRight(bool bHeadTail)
{
    return MoveCaret(true, bHeadTail);
}

bool CInputBox::MoveCaretHead()
{
    return MoveCaret(false, true);
}

bool CInputBox::MoveCaretTail()
{
    return MoveCaret(true, true);
}

int CInputBox::GetFreeInputBufferSize() const
{
    return static_cast<int>(m_dwBufferSize - m_dwCaretPos);
}
