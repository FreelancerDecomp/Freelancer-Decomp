// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail modules import atoi/atol/_wtoi from the system MSVCRT.DLL (e.g. Common.dll
// .text:06310851, Server.dll .text:06d47626, freelancer.exe .text:005ab80c). Those are the
// classic atox.c/wtox.c loops: skip whitespace, one optional sign, then
// total = 10*total + digit with NO overflow check, so the 32-bit total wraps modulo 2^32.
// The port links the UCRT, whose atoi/_wtoi saturate to INT_MAX/INT_MIN since VS2015.
// Data files store 32-bit hash ids in decimal (e.g. ship_archetype = 2806013121) and retail
// reads them back through that wrap; the UCRT would clamp them to 0x7fffffff.
//
// Measured against the system msvcrt.dll (2026-09-26, every byte / every BMP code unit):
//   atoi/atol: "2806013121" -> -1488954175, "4294967296" -> 0, "-2806013121" -> 1488954175,
//              "99999999999" -> 1215752191. Whitespace is exactly 0x09-0x0d and 0x20;
//              digits are exactly '0'-'9' (atol is the same function: long is 32-bit).
//   _wtoi/_wtol: same wrap, no length limit. Whitespace is 0x09-0x0d, 0x20, 0xa0, 0x1680,
//              0x180e, 0x2000-0x200a, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000. Digits are the
//              18 ten-digit runs of the CRT's _wchartodigit (ASCII, Arabic-Indic, ...,
//              Mongolian, fullwidth), all 180 code units confirmed.
// Both helpers use explicit tables so the result does not depend on the process locale.

inline int MsvcrtAtoi(char const* Text)
{
    unsigned char C = static_cast<unsigned char>(*Text);
    while (C == ' ' || (C >= 0x09 && C <= 0x0d))
        C = static_cast<unsigned char>(*++Text);

    unsigned char const Sign = C;
    if (C == '-' || C == '+')
        C = static_cast<unsigned char>(*++Text);

    // Unsigned accumulation gives the same bits as the 32-bit signed wrap without UB.
    unsigned int Total = 0;
    while (C >= '0' && C <= '9')
    {
        Total = 10u * Total + static_cast<unsigned int>(C - '0');
        C = static_cast<unsigned char>(*++Text);
    }

    if (Sign == '-')
        Total = 0u - Total;
    return static_cast<int>(Total);
}

// The CRT's _wchartodigit: value 0-9, or -1 when the code unit is not a decimal digit.
inline int MsvcrtWideCharToDigit(wchar_t C)
{
    static unsigned short const DigitZeros[] = {
        0x0030, 0x0660, 0x06f0, 0x0966, 0x09e6, 0x0a66, 0x0ae6, 0x0b66, 0x0c66,
        0x0ce6, 0x0d66, 0x0e50, 0x0ed0, 0x0f20, 0x1040, 0x17e0, 0x1810, 0xff10,
    };
    unsigned int const Code = static_cast<unsigned int>(C);
    for (unsigned int Index = 0; Index < sizeof(DigitZeros) / sizeof(DigitZeros[0]); ++Index)
    {
        if (Code < DigitZeros[Index])
            return -1;
        if (Code < DigitZeros[Index] + 10u)
            return static_cast<int>(Code - DigitZeros[Index]);
    }
    return -1;
}

inline bool MsvcrtIsWideSpace(wchar_t C)
{
    unsigned int const Code = static_cast<unsigned int>(C);
    return (Code >= 0x09 && Code <= 0x0d) || Code == 0x20 || Code == 0xa0 || Code == 0x1680 ||
           Code == 0x180e || (Code >= 0x2000 && Code <= 0x200a) || Code == 0x2028 ||
           Code == 0x2029 || Code == 0x202f || Code == 0x205f || Code == 0x3000;
}

inline int MsvcrtWtoi(wchar_t const* Text)
{
    wchar_t C = *Text;
    while (MsvcrtIsWideSpace(C))
        C = *++Text;

    wchar_t const Sign = C;
    if (C == L'-' || C == L'+')
        C = *++Text;

    unsigned int Total = 0;
    int Digit = MsvcrtWideCharToDigit(C);
    while (Digit != -1)
    {
        Total = 10u * Total + static_cast<unsigned int>(Digit);
        C = *++Text;
        Digit = MsvcrtWideCharToDigit(C);
    }

    if (Sign == L'-')
        Total = 0u - Total;
    return static_cast<int>(Total);
}
