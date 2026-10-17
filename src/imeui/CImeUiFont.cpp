// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "CImeUiFont.h"
#include <cstring>
#include <cwchar>
#include <vector>

// FUN_067120f0: caret prefixes use an explicit count, not NUL termination.
BOOL ImeUi_GetTextExtent(HDC Dc, const wchar_t* Text, int Count, SIZE* Size)
{
    static bool bChecked = false;
    static bool bWindowsNt = false;
    if (!bChecked)
    {
        bChecked = true;
        OSVERSIONINFOW Version;
        Version.dwOSVersionInfoSize = sizeof(Version);
        #pragma warning(suppress: 4996) // Retail platform branch uses GetVersionExW.
        GetVersionExW(&Version);
        bWindowsNt = Version.dwPlatformId == VER_PLATFORM_WIN32_NT;
    }
    if (bWindowsNt)
        return GetTextExtentPoint32W(Dc, Text, Count, Size);
    int const ByteCount = WideCharToMultiByte(CP_ACP, 0, Text, Count, nullptr, 0, nullptr, nullptr);
    if (ByteCount == 0)
    {
        if (Text == nullptr || *Text != 0)
            return FALSE;
        return GetTextExtentPoint32A(Dc, "", 0, Size);
    }
    std::vector<char> Buffer(ByteCount);
    WideCharToMultiByte(CP_ACP, 0, Text, Count, Buffer.data(), ByteCount, nullptr, nullptr);
    return GetTextExtentPoint32A(Dc, Buffer.data(), ByteCount, Size);
}

CImeUiFont::CImeUiFont(IMEInputBox* Owner)
    : m_hDC(nullptr), m_hFont(nullptr), m_bDirty(false), Color(0), X(0), Y(0), m_pOwner(Owner)
{
    memset(&m_LogFont, 0, sizeof(m_LogFont));
}

CImeUiFont::~CImeUiFont()
{
    // FUN_06712440 calls DeleteObject for the DC too.
    if (m_hDC != nullptr)
        SelectObject(m_hDC, nullptr);
    if (m_hFont != nullptr)
        DeleteObject(m_hFont);
    if (m_hDC != nullptr)
        DeleteObject(m_hDC);
}

void CImeUiFont::UpdateFont()
{
    if (m_bDirty)
    {
        if (m_hFont != nullptr)
            DeleteObject(m_hFont);
        if (m_hDC != nullptr)
            DeleteObject(m_hDC);
        m_hDC = CreateCompatibleDC(nullptr);
        m_hFont = CreateFontIndirectA(&m_LogFont);
        SelectObject(m_hDC, m_hFont);
        m_bDirty = false;
    }
}

void CImeUiFont::SetLogFont(const LOGFONTA* Font)
{
    if (memcmp(&m_LogFont, Font, sizeof(m_LogFont)) != 0)
    {
        memcpy(&m_LogFont, Font, sizeof(m_LogFont));
        m_bDirty = true;
    }
}

void CImeUiFont::SetHeight(int Height)
{
    int const NegativeHeight = -Height;
    if (m_LogFont.lfHeight != NegativeHeight)
    {
        m_LogFont.lfHeight = NegativeHeight;
        m_bDirty = true;
    }
}

void CImeUiFont::SetColor(unsigned InColor)
{
    Color = InColor;
}

void CImeUiFont::SetPosition(int InX, int InY)
{
    X = InX;
    Y = InY;
}

void CImeUiFont::GetTextExtent(const wchar_t* Text, int* Width, int* Height)
{
    *Width = 0;
    *Height = 0;
    if (Text == nullptr || *Text == 0)
        return;
    UpdateFont();
    if (m_hDC != nullptr && m_hFont != nullptr)
    {
        SIZE Size;
        if (ImeUi_GetTextExtent(m_hDC, Text, lstrlenW(Text), &Size))
        {
            *Width = Size.cx;
            *Height = Size.cy;
        }
    }
}

void CImeUiFont::Draw(const wchar_t* Text)
{
    UpdateFont();
    m_pOwner->DrawString(X, Y, Color, Text);
}

HFONT CImeUiFont::GetHFont()
{
    UpdateFont();
    return m_hFont;
}

const LOGFONTA* CImeUiFont::GetLogFont() const
{
    return &m_LogFont;
}
