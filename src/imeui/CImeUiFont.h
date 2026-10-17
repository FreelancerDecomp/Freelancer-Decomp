// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once
#include "imeui.h"

BOOL ImeUi_GetTextExtent(HDC Dc, const wchar_t* Text, int Count, SIZE* Size);

class CImeUiFont : public CImeUiFont_Base
{
public:
    CImeUiFont(IMEInputBox* Owner = nullptr);
    ~CImeUiFont();
    void SetHeight(int Height) override;
    void SetColor(unsigned Color) override;
    void SetPosition(int X, int Y) override;
    void GetTextExtent(const wchar_t* Text, int* Width, int* Height) override;
    void Draw(const wchar_t* Text) override;
    void SetLogFont(const LOGFONTA* Font);
    void UpdateFont();
    HFONT GetHFont();
    const LOGFONTA* GetLogFont() const;

    HDC m_hDC;
    HFONT m_hFont;
    LOGFONTA m_LogFont;
    bool m_bDirty;
    unsigned Color;
    int X;
    int Y;
    IMEInputBox* m_pOwner;
};
