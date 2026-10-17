// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "text_win.h"

#include "common_misc.h"
#include "font_atlas.h"
#include "font_imp.h"
#include "game_options.h"
#include "nn_ui_pulse.h"
#include "space_camera.h"

#include <math.h>
#include <string.h>

TextWin::TextWin()
{
    // The inlined TextWin part of every derived ctor (0x0059c448..0x0059c4a6): the Font::String
    // at +0x7c (FUN_004144b0) is the member's own ctor, then the colours.
    ColorIdle = 0;
    ColorPulse = 0;
    ColorD4 = 0;       // FUN_004b5140
    ColorD8 = 0;       // FUN_004b5140
    ColorInactive = 0; // FUN_004b5140
}

TextWin::~TextWin()
{
    // FUN_005a1af0: clear the text, the Win3D Destroy body (flags, children, unregister),
    // then the Font::String member dtor (inlined at 0x005a1b58..0x005a1bb3, ~FFontString); the
    // Win3D dtor runs as the base.
    FontStringClearContents(&Text);
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

bool TextWin::HitTest(float const* Ray)
{
    // FUN_004b51d0: the ray's point at this object's depth (t = z / ray.z) must sit inside
    // [x, x + width] and [y - height, y]. DAT_005c7474 is 0.0f.
    double const T = static_cast<double>(Position[2]) / Ray[2];
    float const PointY = static_cast<float>(T * Ray[1]);
    double const DeltaX = T * Ray[0] - Position[0];
    if (DeltaX < 0.0)
        return false;
    if (!(DeltaX <= Width))
        return false;
    double const DeltaY = static_cast<double>(Position[1]) - PointY;
    if (DeltaY < 0.0)
        return false;
    if (!(DeltaY <= Height))
        return false;
    return true;
}

bool TextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004b5180 (RET 8).
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);
    InitTextDefaults();
    Width = 0.1f;  // 0x3dcccccd
    Height = 0.1f;
    return true;
}

void TextWin::Destroy()
{
    // FUN_005a1c10.
    FontStringClearContents(&Text);
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

void TextWin::Draw()
{
    // FUN_005a1d40.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if ((Flags & kWin3DVisible) == 0)
        return;

    Bounds[3] = -Height;
    Bounds[0] = 0.0f;
    Bounds[2] = Width;
    Bounds[1] = 0.0f;

    // 0x005a1d8d: under the mouse the pulse built around +0xd0 is used; otherwise the idle
    // colour, or the inactive colour when +0xe6 is clear.
    bool const bHover = Win3DGetHover() == this;
    unsigned long Pulsed = 0;
    NnUiPulseBlendFromArgb(&Pulsed, ColorPulse);
    unsigned long const* Color = &ColorInactive;
    if (bUsePalette != 0)
        Color = bHover ? &Pulsed : &ColorIdle;
    FontAtlasSetDrawColor(static_cast<unsigned>(*Color)); // FUN_00417780

    float ScreenWidth = 0.0f;
    float ScreenHeight = 0.0f;
    TextWinProjectExtent(Width, Height, Position[2], &ScreenWidth, &ScreenHeight);
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    TextWinProjectPoint(Position, &ScreenX, &ScreenY);

    // 0x005a1e33: the draw origin {x, y, width, height, +0xe8, 0} into FUN_004143c0.
    FFontStringDrawOrigin Origin;
    Origin.X = ScreenX;
    Origin.Y = ScreenY;
    Origin.Scale = ScreenWidth;
    Origin.Height = ScreenHeight;
    Origin.Field10 = FieldE8;
    Origin.Field14 = 0;
    FontStringDrawChildren(&Origin, &Text, nullptr, nullptr);
}

void TextWin::SetText(wchar_t const* NewText)
{
    // LAB_005a1cb0: ADD ECX,0x7c / JMP FUN_00415860.
    FontStringSetText(&Text, NewText);
}

void TextWin::InitTextDefaults()
{
    // FUN_005a1c50.
    FlagE0 = 0;
    FlagE3 = 0;
    FlagE1 = 0;
    FlagE2 = 1;
    bUsePalette = 1;
    FieldE8 = 0;
    TextScale = 0.002f; // DAT_005e5f88 = 0x3b03126f
    ColorIdle = G_NnPaletteArgb_679bb0;     // DAT_00679bb0
    ColorPulse = G_NnPaletteArgb_679ba4;    // DAT_00679ba4
    ColorInactive = G_NnPaletteArgb_679b94; // DAT_00679b94
}

void TextWin::SetFont(char const* FontName, float Scale)
{
    // FUN_005a1cc0.
    IFont* const Font = FindMenuFontImp(FontName); // FUN_004177b0
    if (Text.FontIface != Font)
    {
        // 005a1cdb..005a1d0c: store at +0x98, then slot +8 on every child of the vector at
        // +0x8c..+0x90 (8-byte slots, the child pointer first). The call does not look at the
        // slot's kind byte: a FontString child binds the font (LAB_004148b0), a NumberString
        // child takes it as its face key (FUN_00414fb0) -- both are slot +8.
        Text.FontIface = Font;
        for (unsigned Index = 0;
             Text.ChildBegin != nullptr &&
             Index < static_cast<unsigned>(Text.ChildEnd - Text.ChildBegin);
             ++Index)
        {
            Text.ChildBegin[Index].Child->BindFont(Text.FontIface);
        }
        Text.LayoutGen = 0; // +0xa4
    }
    TextScale = Scale; // +0xb0
}

void TextWinProjectExtent(float Width, float Height, float Z, float* OutWidth,
                          float* OutHeight)
{
    // FUN_004176c0. A depth closer than 0.001 (DAT_005c75f8) is taken as 1.0.
    FSpaceCamera const* const Cam = Win3DGetCamera();
    double Depth = fabs(static_cast<double>(Z));
    if (Depth < 0.001)
        Depth = 1.0;
    double const NearOverDepth = static_cast<double>(Cam->ZNear) / Depth;
    double const InvNear = 1.0 / static_cast<double>(Cam->ZNear);
    *OutWidth = static_cast<float>(static_cast<double>(Cam->XScale) * InvNear * NearOverDepth *
                                   Width);
    *OutHeight = static_cast<float>(-(InvNear * Cam->YScale * NearOverDepth * Height));
}

void TextWinProjectPoint(float const* World, float* OutX, float* OutY)
{
    // FUN_00417730.
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (AsCamera(Win3DGetCamera())->Project(&X, &Y, &Z, World) == 0)
    {
        X = 0.0f;
        Y = 0.0f;
    }
    *OutX = X;
    *OutY = Y;
}
