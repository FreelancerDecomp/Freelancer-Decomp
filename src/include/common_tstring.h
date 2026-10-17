// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TString<N> on its own so a header that only needs the fixed-size string (common_universe.h,
// for the SYSTEM object record) does not have to pull common_reputation.h and, through
// common_text.h, <windows.h>. common_reputation.h includes this header, so every existing user
// still sees the template where it saw it before.

#include <cstring>

// Retail TString<N>: length at +0, chars at +4. Mangle ?$TString@$0BA@ for N=16.
template <unsigned N>
struct TString
{
    unsigned Length;
    char Data[N];

    // freelancer.exe 0x004c9610 (inlined per-N template instantiation): compares Data against a
    // plain C string. Retail walks two bytes per loop iteration (an unrolled strcmp); this port
    // does the equivalent one byte at a time. A null Str matches only an empty TString.
    bool operator==(char const* Str) const
    {
        if (Str == nullptr)
            return Length == 0;
        unsigned i = 0;
        for (;; ++i)
        {
            if (Data[i] != Str[i])
                return false;
            if (Data[i] == '\0')
                return true;
        }
    }

    // Common.dll FUN_062fb910 (the N=64 instance, not exported; also inlined into
    // CommReferrable::operator= .text:062610a0, CommReferrable::read_comm_ini_data
    // .text:0632e7b0 and Server.dll pub::Translate{Good,Ship}ToMsgIdPrefix .text:06d5e050/
    // .text:06d5e0d0): a null Str empties the string; otherwise strlen, clamped to N-1
    // (`CMP EAX,N; JC; MOV EAX,N-1`), copied without the terminator, then Data[Len] = 0.
    // Common.dll FUN_06306020 (`len = strlen; if (len >= N) len = N-1; copy; NUL`) is the same
    // operation spelled as a free function.
    TString& operator=(char const* Str)
    {
        if (Str == nullptr)
        {
            Length = 0;
            Data[0] = '\0';
            return *this;
        }
        unsigned Len = static_cast<unsigned>(strlen(Str));
        if (Len >= N)
            Len = N - 1;
        memcpy(Data, Str, Len);
        Data[Len] = '\0';
        Length = Len;
        return *this;
    }
};
