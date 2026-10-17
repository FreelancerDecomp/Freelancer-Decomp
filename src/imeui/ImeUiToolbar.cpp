// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ImeUiCore.h"
#include <cmath>

// FUN_06715600: sixteen 0x20-byte XYZRHW/color vertices, then the symbol font.
struct FIMEToolbarVertex
{
    float X;
    float Y;
    float Z;
    float RHW;
    unsigned Color;
    unsigned Reserved[3];
};
static_assert(sizeof(FIMEToolbarVertex) == 0x20, "IME draw-fan vertex stride");

static double GToolbarLastTick = 0.0; // DAT_0671cee0; written only by this function.
static unsigned GToolbarPhase = 0;   // DAT_0671cfc0; zero-initialized retail .data.

void ImeUi_RenderToolbar(const _IMEUI_APPEARANCE& Appearance, const RECT& Bounding,
                        int ScreenWidth, int ScreenHeight, const DWORD& State,
                        const DWORD& Language, const wchar_t* const& Symbol)
{
    const bool bActive = State != 0;
    const float Diameter = static_cast<float>(Appearance.SymbolHeight);
    const double Radius = static_cast<double>(Diameter) * 0.5;
    FIMEToolbarVertex Vertices[16] = {};
    // Screen dimensions are loaded as zero-extended qwords by retail FILD.
    const double Width = static_cast<unsigned>(ScreenWidth);
    const double Height = static_cast<unsigned>(ScreenHeight);
    switch (Appearance.SymbolPosition)
    {
    case 0:
        // FCOMPP Width vs Diameter+right+4; TEST AH,05 + JP selects
        // the right side on greater/equal (both masked bits zero).
        if (Width >= static_cast<double>(Diameter) + Bounding.right + 4.0)
            Vertices[0].X = static_cast<float>((static_cast<double>(Bounding.right) - Radius) + Diameter + 4.0);
        else
            Vertices[0].X = static_cast<float>(static_cast<double>(Diameter) * -0.5 + Bounding.left - 4.0);
        Vertices[0].Y = static_cast<float>(static_cast<double>(Bounding.bottom) - (Appearance.SymbolHeight >> 1));
        break;
    case 1:
        Vertices[0].X = static_cast<float>(Radius + 4.0);
        Vertices[0].Y = static_cast<float>(Radius + 4.0);
        break;
    case 2:
        Vertices[0].X = static_cast<float>(Width - (Radius + 4.0));
        Vertices[0].Y = static_cast<float>(Radius + 4.0);
        break;
    case 3:
        Vertices[0].X = static_cast<float>(Width - (Radius + 4.0));
        Vertices[0].Y = static_cast<float>(Height - (Radius + 4.0));
        break;
    case 4:
        Vertices[0].X = static_cast<float>(Radius + 4.0);
        Vertices[0].Y = static_cast<float>(Height - (Radius + 4.0));
        break;
    }
    Vertices[0].RHW = 1.0f;

    if (bActive)
    {
        // FCOMP 250; AND EAX,4100 + JNZ skips update for <= or unordered.
        // Fall-through (greater) advances exactly once, without catch-up.
        if (static_cast<double>(GetTickCount()) - GToolbarLastTick > 250.0)
        {
            ++GToolbarPhase;
            GToolbarLastTick = static_cast<double>(GetTickCount());
            if (GToolbarPhase > 13) GToolbarPhase = 0;
        }
    }
    else GToolbarPhase = 0;

    const float AngleStep = 0.4487989246845245361328125f; // PE 0x3ee5c8f9
    unsigned Step = bActive ? GToolbarPhase : 0;
    for (unsigned Index = 1; Index < 16; ++Index, ++Step)
    {
        const double Angle = static_cast<double>(Step) * AngleStep;
        // Radius/angle stay double; FSTP dword rounds each final coordinate.
        Vertices[Index].X = static_cast<float>(Radius * std::cos(Angle) + Vertices[0].X);
        Vertices[Index].Y = static_cast<float>(Radius * std::sin(Angle) + Vertices[0].Y);
        Vertices[Index].RHW = 1.0f;
    }
    const unsigned Alpha = static_cast<unsigned>(Appearance.SymbolAlpha) << 24;
    Vertices[0].Color = Alpha + 0xffffff;
    const unsigned Rainbow[15] = {
        0xff0000, 0xff3000, 0xff6000, 0xff9000, 0xffc000,
        0xffff00, 0xc0ff00, 0x90ff00, 0x60ff00, 0x30c0ff,
        0x00a0ff, 0x3090ff, 0x6060ff, 0x9030ff, 0xc000ff
    };
    for (unsigned Index = 1; Index < 16; ++Index)
    {
        unsigned Color;
        if (Appearance.SymbolOnColor == 0 && bActive) Color = Rainbow[Index - 1];
        else Color = bActive ? Appearance.SymbolOnColor : Appearance.SymbolOffColor;
        Vertices[Index].Color = Alpha + Color;
    }
    GDrawFan(Vertices, 16);

    // Retail rereads SymbolHeight after the callback, and uses Language&3ff.
    double FontHeight = static_cast<double>(Appearance.SymbolHeight) * 0.625f;
    if ((Language & 0x3ff) == 0x12 && bActive) FontHeight *= 0.800000011920928955078125f;
    CImeUiFont_Base* Font = Appearance.SymbolFont;
    if (!Font) return;
    FontHeight *= 1.33333337306976318359375f;
    Font->SetHeight(static_cast<int>(FontHeight)); // retail ftol truncates.
    Appearance.SymbolFont->SetColor((static_cast<unsigned>(Appearance.SymbolAlpha) << 24) | Appearance.SymbolBackgroundColor);
    const wchar_t* Text = State == 1 ? Symbol : L"A";
    int TextWidth;
    int TextHeight;
    Appearance.SymbolFont->GetTextExtent(Text, &TextWidth, &TextHeight);
    // SHR on extents is unsigned; each center coordinate goes through ftol
    // before subtracting the half extent (not rounding the difference).
    const int X = static_cast<int>(Vertices[0].X) - (static_cast<unsigned>(TextWidth) >> 1);
    const int Y = static_cast<int>(Vertices[0].Y) - (static_cast<unsigned>(TextHeight) >> 1);
    Appearance.SymbolFont->SetPosition(X, Y);
    Appearance.SymbolFont->Draw(Text);
}
