// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

#include <cstdio>
#include <cstring>

// ============================================================================
// File class — CRT FILE* wrapper, retail layout (sizeof 0x8C, see common_misc.h)

File::File()
{
    // Common.dll.txt 0630b270-0630b288: byte Name[0] = 0, Handle (+0x80) = 0,
    // Length (+0x88) = 0, Offset (+0x84) = 0. The rest of Name stays uninitialised.
    Name[0] = '\0';
    Handle = nullptr;
    Length = 0;
    Offset = 0;
}

File::~File()
{
    // Common.dll.txt 0630b290: JMP File::close.
    close();
}

bool File::open(char const* Path)
{
    // Common.dll.txt 0630cdc0-0630ce41. A null or empty Path skips the whole body (the
    // result is the existing handle's null-ness). Otherwise the path is byte-copied into
    // Name (0630cdd4, unbounded: a path of 0x80+ bytes is undefined behaviour in retail;
    // the port stops at the buffer), Handle = fopen(Name, "rb" @063a3568) - an already
    // open handle is overwritten without fclose, as retail - and on success
    // fseek(END), Length = ftell, Offset = 0, fseek(SET).
    if (Path != nullptr && Path[0] != '\0')
    {
        size_t Index = 0;
        for (; Index < sizeof(Name) - 1 && Path[Index] != '\0'; ++Index)
            Name[Index] = Path[Index];
        Name[Index] = '\0';

        Handle = fopen(Name, "rb");
        if (Handle != nullptr)
        {
            fseek(Handle, 0, SEEK_END);
            Length = (int)ftell(Handle);
            Offset = 0;
            fseek(Handle, 0, SEEK_SET);
        }
    }
    return Handle != nullptr;
}

void File::close()
{
    // Common.dll.txt 0630cd40-0630cd62: fclose + null the handle only; Offset/Length stay.
    if (Handle != nullptr)
    {
        fclose(Handle);
        Handle = nullptr;
    }
}

int File::read(void* Buffer, int Bytes)
{
    // Common.dll.txt 0630cec0-0630cee3: no handle -> 0 (EAX is the null handle);
    // otherwise fread(Buffer, 1, Bytes, Handle). Offset is not updated.
    if (Handle == nullptr)
        return 0;
    return (int)fread(Buffer, 1, Bytes, Handle);
}

int File::read(void* Buffer, int OffsetFromStart, int Bytes)
{
    // Common.dll.txt 06262de0-06262dff: seek(param_2), then read(param_1, param_3).
    seek(OffsetFromStart);
    return read(Buffer, Bytes);
}

int File::read_line(char* Line, int MaxLen)
{
    // Common.dll.txt 0630cef0-0630cfb3, register for register:
    //   Count (ESI) = 0; no handle, null Line or MaxLen <= 0 -> return 0.
    //   Limit (EDI) = MaxLen - 1. Limit <= 0: the last-char register (EAX) is loaded with
    //   the Line pointer itself (0630cf4e) and the tail below runs with it - it is neither
    //   -1 nor '\r', so the result is Line[0] = 0, return 1 after ftell.
    //   Loop: c = fgetc; -1 -> tail-or-0; '\r' / '\n' -> tail; else Line[Count++] = c,
    //   stop at Count == Limit (c is then the last stored char).
    //   Tail: c == -1 && Count == 0 -> return 0. Line[Count++] = 0; c == '\r' -> peek the
    //   next char and fseek(-1, SEEK_CUR) unless it is '\n'; Offset = ftell; return Count.
    int Count = 0;
    if (Handle == nullptr || Line == nullptr || MaxLen <= 0)
        return 0;

    int const Limit = MaxLen - 1;
    int Last = 0; // Limit <= 0: retail holds the Line pointer here - never -1 or '\r'
    if (Limit > 0)
    {
        for (;;)
        {
            Last = fgetc(Handle);
            if (Last == -1 || Last == '\r' || Last == '\n')
                break;
            Line[Count] = (char)Last;
            ++Count;
            if (Count >= Limit)
                break;
        }
    }

    if (Last == -1 && Count == 0)
        return 0;

    Line[Count] = '\0';
    ++Count;
    if (Last == '\r')
    {
        if (fgetc(Handle) != '\n')
            fseek(Handle, -1, SEEK_CUR);
    }
    Offset = (int)ftell(Handle);
    return Count;
}

bool File::is_open()
{
    // Common.dll.txt 06262d80: Handle != 0.
    return Handle != nullptr;
}

int File::eof()
{
    // Common.dll.txt 06262d60-06262d77: SETGE on Offset vs Length (signed).
    return Offset >= Length ? 1 : 0;
}

int File::tell() const
{
    // Common.dll.txt 0x06262dd0: MOV EAX,[this+0x84]; RET -- the cached Offset (retail +0x84
    // is the field set_offset/seek maintain). No handle check, no ftell.
    return Offset;
}

int File::seek(int NewOffset)
{
    // Common.dll.txt 0630ce50-0630ceae. -1 is a pure query. Otherwise Offset changes only
    // for a different value in [0, Length]; then, with a handle, fseek(Handle, Offset, SET)
    // == 0 returns Offset; no handle or a failed fseek returns 0.
    if (NewOffset == -1)
        return Offset;
    if (NewOffset != Offset && NewOffset >= 0 && NewOffset <= Length)
        Offset = NewOffset;
    if (Handle != nullptr)
    {
        if (fseek(Handle, Offset, SEEK_SET) == 0)
            return Offset;
    }
    return 0;
}

int File::get_length()
{
    // Common.dll.txt 06262d50: MOV EAX,[this+0x88] - the cached Length (ICF-shared with
    // several archetype getters).
    return Length;
}

void File::get_info()
{
    // Common.dll.txt 0630cd70-0630cdb6: fseek(END), Length = ftell, Offset = 0,
    // fseek(SET). No handle check in retail (callers only reach it with an open handle).
    fseek(Handle, 0, SEEK_END);
    Length = (int)ftell(Handle);
    Offset = 0;
    fseek(Handle, 0, SEEK_SET);
}

void File::set_name(char const* NewName)
{
    // Common.dll.txt 06262d90-06262da2: byte copy into Name up to and including the NUL
    // (unbounded in retail - a name of 0x80+ bytes is undefined behaviour; the port stops
    // at the buffer).
    size_t Index = 0;
    for (; Index < sizeof(Name) - 1 && NewName[Index] != '\0'; ++Index)
        Name[Index] = NewName[Index];
    Name[Index] = '\0';
}

int File::set_offset(int NewOffset)
{
    // Common.dll.txt 06262db0-06262dcc: store NewOffset when 0 <= NewOffset <= Length
    // (signed JL / JG); return Offset.
    if (NewOffset >= 0 && NewOffset <= Length)
        Offset = NewOffset;
    return Offset;
}

char const* File::get_name() const
{
    // Common.dll.txt 06261260: MOV EAX,this - Name is at +0.
    return Name;
}

File& File::operator=(File const& Other)
{
    // Common.dll.txt 06262e10-06262e23: MOVSD x 0x23 - a raw 0x8C-byte copy; the copy
    // shares the source's FILE*.
    memmove((void*)this, &Other, sizeof(File)); // memmove: self-assignment
    return *this;
}
