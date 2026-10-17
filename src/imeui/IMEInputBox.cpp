// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "IMEInputBox.h"
#include "IMEFocus.h"
#include "ImeUiCore.h"

IMEInputBox::IMEInputBox(const LOGFONTA* Font)
    : CInputBox()
{
    m_pFont = new CImeUiFont(this);
    IMEFocus* Focus = IMEFocus::GetSingleton();
    if (Focus)
    {
        UpdateAppearance();
        Focus->RegisterInputBox(this);
    }
    if (Font) SetFont(const_cast<LOGFONTA*>(Font));
}

IMEInputBox::IMEInputBox(const IMEInputBox& Other)
    : CInputBox(Other), m_pFont(Other.m_pFont)
{
}

IMEInputBox::~IMEInputBox()
{
    IMEFocus* Focus = IMEFocus::GetSingleton();
    if (Focus) Focus->RemoveInputBox(this);
    delete m_pFont;
    m_pFont = NULL;
}

IMEInputBox& IMEInputBox::operator=(const IMEInputBox& Other)
{
    CInputBox::operator=(Other);
    m_pFont = Other.m_pFont;
    return *this;
}

void IMEInputBox::OnSetFocus()
{
    ImeUi_SetState(true);
    CInputBox::OnSetFocus();
    UpdateAppearance();
}

void IMEInputBox::OnKillFocus()
{
    CInputBox::OnKillFocus();
    ImeUi_FinalizeString(true);
    ImeUi_SetState(false);
}

void IMEInputBox::Resize(const RECT* pRect)
{
    CInputBox::Resize(pRect);
    UpdateAppearance();
}

void IMEInputBox::ImeShowToolbar(bool bShow)
{
    // Retail 0x06711110 is RET 4 (ICF-shared with UpdatedText).
}

void IMEInputBox::ToggleInsertMode()
{
    CInputBox::ToggleInsertMode();
    ImeUi_SetInsertMode(m_bInsertMode);
    m_Caret.SetBlink(m_bInsertMode);
}

bool IMEInputBox::CalcCaretPos(POINT* Position)
{
    if (!m_pFont || !m_pBuffer || m_pBuffer[0] == 0) return false;
    m_pFont->UpdateFont();
    return ImeUi_GetTextExtent(m_pFont->m_hDC, reinterpret_cast<wchar_t*>(m_pBuffer),
                             static_cast<int>(m_dwCaretPos), reinterpret_cast<SIZE*>(Position));
}

void IMEInputBox::UpdateAppearance()
{
    if (m_bHasFocus)
    {
        IMEFocus* Focus = IMEFocus::GetSingleton();
        if (Focus) ImeUi_SetAppearance(Focus->GetDefaultAppearance());
        ImeUi_SetFontAppearance(m_pFont, 0, &m_rcBounding);
    }
}

HFONT IMEInputBox::GetHFont()
{
    return m_pFont ? m_pFont->GetHFont() : NULL;
}

const LOGFONTA* IMEInputBox::GetFont()
{
    return m_pFont ? m_pFont->GetLogFont() : NULL;
}

bool IMEInputBox::IsImeOn() const
{
    return m_bHasFocus && ImeUi_GetState() == 1;
}

void IMEInputBox::SetFont(LOGFONTA* Font)
{
    if (m_pFont) m_pFont->SetLogFont(Font);
    UpdateAppearance();
    ResetCaret();
}
