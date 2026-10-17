// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Rect and the Rect / ViewRect / RECT conversions -- split out of common_text.cpp in CMN-SPLIT
// phase B.

#include "common_text.h"

// ============================================================================
// Rect — all methods from Ghidra, straightforward arithmetic.
// ============================================================================

Rect::Rect()
{
    Left = 0;
    Top = 0;
    Width = 0;
    Height = 0;
}

Rect::Rect(int InLeft, int InTop, int InWidth, int InHeight)
{
    Left = InLeft;
    Top = InTop;
    Width = InWidth;
    Height = InHeight;
}

void Rect::clear()
{
    Left = 0;
    Top = 0;
    Width = 0;
    Height = 0;
}

void Rect::init(int InLeft, int InTop, int InWidth, int InHeight)
{
    Left = InLeft;
    Top = InTop;
    Width = InWidth;
    Height = InHeight;
}

void Rect::init_from_ltrb(int InLeft, int InTop, int InRight, int InBottom)
{
    // RVA 0x81590: left=top, width=(right-left)+1, top=top, height=(bottom-top)+1
    Left = InLeft;
    Top = InTop;
    Width = (InRight - InLeft) + 1;
    Height = (InBottom - InTop) + 1;
}

int Rect::rightmost() const
{
    // RVA 0x815C0: left + width - 1
    return Left + Width - 1;
}

int Rect::bottommost() const
{
    // RVA 0x815D0: top + height - 1
    return Top + Height - 1;
}

int Rect::area() const
{
    // RVA 0x815E0: width * height
    return Width * Height;
}

bool Rect::null() const
{
    // RVA 0x815F0: true if width==0 OR height==0
    if (Width != 0 && Height != 0)
    {
        return false;
    }
    return true;
}

bool Rect::nonnull() const
{
    // RVA 0x81610: true if width>0 AND height>0
    if (Width > 0 && Height > 0)
    {
        return true;
    }
    return false;
}

int Rect::leftmost() const
{
    // RVA 0x01D90 — returns Left field directly
    return Left;
}

int Rect::topmost() const
{
    // RVA 0x71AD0 — returns Top field directly
    return Top;
}

int Rect::width() const
{
    // RVA 0x466A0 — returns Width field directly
    return Width;
}

int Rect::height() const
{
    // RVA 0x3FB90 — returns Height field directly
    return Height;
}

void Rect::get_center(int* OutX, int* OutY) const
{
    // RVA 0xBE2A0. BUG FIX (2026-09-06, batch10a): this previously read
    // `Left + Width / 2` / `Top + Height / 2`, which is off by one from retail for
    // even Width/Height. Retail computes `(2*Left + Width - 1)` (resp. Height) via
    // LEA then divides by 2 with a CDQ/SUB/SAR sequence that reproduces C's
    // truncating signed division exactly -- i.e. `(2*Left + Width - 1) / 2`, NOT
    // `Left + Width / 2`. These differ by 1 whenever Width/Height is even (e.g.
    // Left=0, Width=4: retail center.x = (0+4-1)/2 = 1, the old code gave 2).
    if (OutX)
    {
        *OutX = (2 * Left + Width - 1) / 2;
    }
    if (OutY)
    {
        *OutY = (2 * Top + Height - 1) / 2;
    }
}

bool Rect::point_is_inside(int X, int Y) const
{
    // RVA 0xBE2E0
    if (X >= Left && X < Left + Width && Y >= Top && Y < Top + Height)
    {
        return true;
    }
    return false;
}

bool operator==(Rect const& A, Rect const& B)
{
    // RVA 0xBE320. Fields-equal OR both-null (Width==0 or Height==0 counts as "null",
    // and two null rects compare equal regardless of their other field values).
    bool const FieldsEqual = (A.Left == B.Left) && (A.Top == B.Top) &&
                              (A.Width == B.Width) && (A.Height == B.Height);
    bool const BothNull = A.null() && B.null();
    return FieldsEqual || BothNull;
}

bool operator!=(Rect const& A, Rect const& B)
{
    // RVA 0xBE390. Independently inlined by the compiler from operator==: fields-differ
    // check is the same, but the "at least one side non-empty" check here compares
    // Width/Height against >0 (signed JLE/JG) rather than !=0 (JZ/JNZ) like operator==
    // does via Rect::null(). Reproduced exactly rather than written as !(A==B), since
    // that would change behavior for a Rect with a negative Width or Height.
    bool const FieldsDiffer = (A.Left != B.Left) || (A.Top != B.Top) ||
                               (A.Width != B.Width) || (A.Height != B.Height);
    bool const ANonEmpty = (A.Width > 0) && (A.Height > 0);
    bool const BNonEmpty = (B.Width > 0) && (B.Height > 0);
    return FieldsDiffer && (ANonEmpty || BNonEmpty);
}

ViewRect __cdecl Rect_to_ViewRect(Rect const& In)
{
    // Common.dll.txt:0631e5f0. Right/Bottom are inclusive, hence the -1.
    ViewRect Out;
    Out.Left = In.Left;
    Out.Top = In.Top;
    Out.Right = In.Width + In.Left - 1;
    Out.Bottom = In.Height + In.Top - 1;
    return Out;
}

Rect __cdecl ViewRect_to_Rect(ViewRect const& In)
{
    // Common.dll.txt:0631e620, the exact inverse.
    Rect Out;
    Out.Left = In.Left;
    Out.Top = In.Top;
    Out.Width = In.Right - In.Left + 1;
    Out.Height = In.Bottom - In.Top + 1;
    return Out;
}

tagRECT __cdecl Rect_to_RECT(Rect const& In)
{
    tagRECT Out;
    Out.left = In.Left;
    Out.top = In.Top;
    Out.right = In.Left + In.Width - 2;
    Out.bottom = In.Top + In.Height - 2;
    return Out;
}

Rect __cdecl RECT_to_Rect(tagRECT const& In)
{
    Rect Out;
    Out.Left = In.left;
    Out.Top = In.top;
    Out.Width = In.right - In.left + 2;
    Out.Height = In.bottom - In.top + 2;
    return Out;
}

COMMON_API Rect __cdecl rect_intersect(Rect const& A, Rect const& B)
{
    int ALeft = A.Left;
    int BLeft = B.Left;
    int ARight = ALeft - 1 + A.Width;
    int BRight = BLeft - 1 + B.Width;

    int OutLeft = (ALeft >= BLeft) ? ALeft : BLeft;
    int OutRight = (ARight <= BRight) ? ARight : BRight;

    if (OutLeft <= OutRight)
    {
        int ATop = A.Top;
        int BTop = B.Top;
        int ABottom = ATop - 1 + A.Height;
        int BBottom = BTop - 1 + B.Height;

        int OutTop = (ATop >= BTop) ? ATop : BTop;
        int OutBottom = (ABottom <= BBottom) ? ABottom : BBottom;

        if (OutTop <= OutBottom)
        {
            Rect Out;
            Out.Left = OutLeft;
            Out.Top = OutTop;
            Out.Width = OutRight - OutLeft + 1;
            Out.Height = OutBottom - OutTop + 1;
            return Out;
        }
    }

    Rect Out;
    Out.Left = 0;
    Out.Top = 0;
    Out.Width = 0;
    Out.Height = 0;
    return Out;
}

COMMON_API Rect __cdecl rect_union(Rect const& A, Rect const& B)
{
    if (A.Width == 0 || A.Height == 0)
    {
        return B;
    }

    if (B.Width != 0 && B.Height != 0)
    {
        int ALeft = A.Left;
        int ARight = ALeft - 1 + A.Width;
        int ATop = A.Top;
        int ABottom = ATop - 1 + A.Height;

        int BLeft = B.Left;
        int BRight = BLeft - 1 + B.Width;
        int BTop = B.Top;
        int BBottom = BTop - 1 + B.Height;

        int OutLeft = (ALeft < BLeft) ? ALeft : BLeft;
        int OutRight = (ARight > BRight) ? ARight : BRight;
        int OutTop = (ATop < BTop) ? ATop : BTop;
        int OutBottom = (ABottom > BBottom) ? ABottom : BBottom;

        Rect Out;
        Out.Left = OutLeft;
        Out.Top = OutTop;
        Out.Width = OutRight - OutLeft + 1;
        Out.Height = OutBottom - OutTop + 1;
        return Out;
    }

    return A;
}
