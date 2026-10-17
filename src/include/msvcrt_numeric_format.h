// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#ifndef MSVCRT_NUMERIC_FORMAT_H
#define MSVCRT_NUMERIC_FORMAT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <locale.h>

/* Selected VC6 numeric contracts used by Lua and NumberStringChild.
 * Keep options local: changing the module-wide UCRT policy affects other users. */
static int MsvcrtLegacyVsnprintf(char* Buffer, size_t Count, char const* Format, va_list Args)
{
    return __stdio_common_vsprintf(
        _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION |
        _CRT_INTERNAL_PRINTF_LEGACY_MSVCRT_COMPATIBILITY |
        _CRT_INTERNAL_PRINTF_LEGACY_THREE_DIGIT_EXPONENTS,
        Buffer, Count, Format, NULL, Args);
}

static int MsvcrtLegacySnprintf(char* Buffer, size_t Count, char const* Format, ...)
{
    int Result;
    va_list Args;
    va_start(Args, Format);
    Result = MsvcrtLegacyVsnprintf(Buffer, Count, Format, Args);
    va_end(Args);
    return Result;
}

static void MsvcrtRoundDigits(char* Digits, int Keep, int* Point)
{
    int Index;
    if (Keep < 0 || Keep >= 17 || Digits[Keep] < '5')
        return;
    Index = Keep - 1;
    while (Index >= 0 && Digits[Index] == '9')
        Digits[Index--] = '0';
    if (Index >= 0)
        ++Digits[Index];
    else
    {
        Digits[0] = '1';
        for (Index = 1; Index < 17; ++Index)
            Digits[Index] = '0';
        ++*Point;
    }
}

static int MsvcrtIsSpecial(double Value)
{
    uint64_t Bits;
    memcpy(&Bits, &Value, sizeof(Bits));
    return (Bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000);
}

static void MsvcrtFormatLuaNumber(char* Buffer, double Value)
{
    char Digits[32];
    char* Cursor = Buffer;
    int Point, Sign, Length, Index;
    char Decimal = localeconv()->decimal_point[0];
    if (MsvcrtIsSpecial(Value))
    {
        MsvcrtLegacySnprintf(Buffer, 32, "%.16g", Value);
        return;
    }
    _ecvt_s(Digits, sizeof(Digits), Value, 17, &Point, &Sign);
    if (Value == 0.0)
        Point = 1; // %g emits 0/-0; _ecvt_s reports a decimal point at zero.
    MsvcrtRoundDigits(Digits, 16, &Point);
    Length = 16;
    while (Length > 1 && Digits[Length - 1] == '0')
        --Length;
    if (Sign)
        *Cursor++ = '-';
    if (Point - 1 < -4 || Point - 1 >= 16)
    {
        *Cursor++ = Digits[0];
        if (Length > 1)
        {
            *Cursor++ = Decimal;
            memcpy(Cursor, Digits + 1, Length - 1);
            Cursor += Length - 1;
        }
        MsvcrtLegacySnprintf(Cursor, 32 - (size_t)(Cursor - Buffer),
            "e%c%03d", Point - 1 < 0 ? '-' : '+', abs(Point - 1));
        return;
    }
    if (Point <= 0)
    {
        *Cursor++ = '0';
        *Cursor++ = Decimal;
        for (Index = 0; Index < -Point; ++Index)
            *Cursor++ = '0';
        memcpy(Cursor, Digits, Length);
        Cursor += Length;
    }
    else
    {
        for (Index = 0; Index < Point; ++Index)
            *Cursor++ = Index < Length ? Digits[Index] : '0';
        if (Length > Point)
        {
            *Cursor++ = Decimal;
            memcpy(Cursor, Digits + Point, Length - Point);
            Cursor += Length - Point;
        }
    }
    *Cursor = 0;
}

/* NumberStringChild passes a widened float and a single %.Nf with a suffix.
 * VC6's fixed output pads zeros after its 17 significant decimal digits. */
static int MsvcrtNumericVsnprintf(char* Buffer, size_t Count, char const* Format, va_list Args)
{
    char const* Cursor = Format;
    int Precision = 0;
    if (Cursor[0] == '%' && Cursor[1] == '.' && Cursor[2] >= '0' && Cursor[2] <= '9')
    {
        Cursor += 2;
        while (*Cursor >= '0' && *Cursor <= '9' && Precision <= 100)
            Precision = Precision * 10 + (*Cursor++ - '0');
        if (*Cursor == 'f' && Precision <= 100 &&
            (strchr(Cursor + 1, '%') == NULL || strcmp(Cursor + 1, "%") == 0 || strcmp(Cursor + 1, "%%") == 0))
        {
            double Value = va_arg(Args, double);
            char Digits[32], Output[512];
            char* Out = Output;
            char const* Suffix = strcmp(Cursor + 1, "%") == 0 ? "" :
                (strcmp(Cursor + 1, "%%") == 0 ? "%" : Cursor + 1);
            int Point, Sign, Index, Keep, Length;
            if (MsvcrtIsSpecial(Value))
                return MsvcrtLegacySnprintf(Buffer, Count, Format, Value);
            _ecvt_s(Digits, sizeof(Digits), Value, 17, &Point, &Sign);
            Keep = Point + Precision;
            MsvcrtRoundDigits(Digits, Keep, &Point);
            if (Keep >= 0 && Point + Precision > Keep)
                Keep = Point + Precision;
            if (Sign)
                *Out++ = '-';
            for (Index = 0; Index < (Point > 0 ? Point : 1); ++Index)
                *Out++ = Point > 0 && Index < 17 && Index < Keep ? Digits[Index] : '0';
            if (Precision)
                *Out++ = localeconv()->decimal_point[0];
            for (Index = 0; Index < Precision; ++Index)
            {
                int Digit = Point + Index;
                *Out++ = Digit >= 0 && Digit < 17 && Digit < Keep ? Digits[Digit] : '0';
            }
            Length = (int)(Out - Output);
            if (Length + strlen(Suffix) >= sizeof(Output))
                return -1;
            strcpy(Out, Suffix);
            Length += (int)strlen(Suffix);
            if ((size_t)Length >= Count)
            {
                memcpy(Buffer, Output, Count);
                return (size_t)Length == Count ? Length : -1;
            }
            memcpy(Buffer, Output, Length + 1);
            return Length;
        }
    }
    return MsvcrtLegacyVsnprintf(Buffer, Count, Format, Args);
}
#endif
