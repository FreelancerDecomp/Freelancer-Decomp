// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll text-file layer: FileMap (a file mapped, or a buffer copied, into memory),
// LineParser32 (one line and its read cursor) and TextFile32 (FileMap + line counter).
// Split out of common_ini.cpp; INI_Reader (common_ini.cpp) reads text INIs through these.

#include "common_ini.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

FileMap::FileMap()
    : FileHandle(INVALID_HANDLE_VALUE)
    , MappingHandle(nullptr)
    , View(nullptr)
    , Size(0)
    , Cursor(0)
{
    // .text:06263040: empty string, +0x10 = -1, +0x14..+0x20 = 0.
}

FileMap::~FileMap()
{
    // .text:06263310: the same body as close() inlined, then the string dtor.
    close();
}

FileMap::FileMap(FileMap const& Other)
    : FileName(Other.FileName)
    , FileHandle(Other.FileHandle)
    , MappingHandle(Other.MappingHandle)
    , View(Other.View)
    , Size(Other.Size)
    , Cursor(Other.Cursor)
{
    // .text:06262ac0: string copy, then +0x10..+0x20 copied verbatim (.text:06262bd5-06262bf1).
}

FileMap& FileMap::operator=(FileMap const& Other)
{
    // .text:06262c00: string assign, then +0x10..+0x20 copied verbatim
    // (.text:06262d29-06262d45). Nothing previously held by `this` is closed.
    FileName = Other.FileName;
    FileHandle = Other.FileHandle;
    MappingHandle = Other.MappingHandle;
    View = Other.View;
    Size = Other.Size;
    Cursor = Other.Cursor;
    return *this;
}

void FileMap::close()
{
    // .text:06262630.
    FileName.clear();
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        if (MappingHandle != nullptr)
        {
            if (View != nullptr)
            {
                UnmapViewOfFile(View);
                View = nullptr;
                Size = 0;
            }
            CloseHandle(MappingHandle);
            MappingHandle = nullptr;
        }
        CloseHandle(FileHandle);
        FileHandle = INVALID_HANDLE_VALUE;
    }
    if (View != nullptr)
    {
        free(View);
        View = nullptr;
        Size = 0;
    }
    Cursor = 0;
}

bool FileMap::open(char const* Path)
{
    // .text:06262750: close() inlined, remember the name, then map the whole file read-only.
    close();
    FileName = Path;

    FileHandle = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        MappingHandle = CreateFileMappingA(FileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (MappingHandle != nullptr)
        {
            Size = GetFileSize(FileHandle, nullptr);
            View = (char*)MapViewOfFile(MappingHandle, FILE_MAP_READ, 0, 0, Size);
            if (View != nullptr)
            {
                Cursor = 0;
                return true;
            }
        }
    }

    // .text:0626289c: any failure drops the name and runs close() inlined again.
    close();
    return false;
}

bool FileMap::open_memory(void const* Buffer, unsigned int Bytes)
{
    // .text:06262980: no close() first; a null buffer or zero length fails without touching
    // anything. The malloc result is not checked.
    if (Buffer == nullptr || Bytes == 0)
        return false;

    View = (char*)malloc(Bytes);
    memcpy(View, Buffer, Bytes);
    Size = Bytes;
    Cursor = 0;
    return true;
}

void FileMap::seek(unsigned int Pos)
{
    // .text:06262a10.
    if (View == nullptr)
        return;
    Cursor = (Pos <= Size) ? Pos : Size;
}

unsigned int FileMap::tell() const
{
    // .text:06262a00.
    if (View != nullptr)
        return Cursor;
    return 0;
}

bool FileMap::eof() const
{
    // .text:062629f0: !(Cursor < Size), no View check.
    return !(Cursor < Size);
}

unsigned int FileMap::get_file_length() const
{
    // .text:062629e0: MOV EAX,[this+0x1c].
    return Size;
}

char const* FileMap::get_file_name() const
{
    // .text:062629d0: the string's buffer, or the library's empty string when it has none.
    return FileName.c_str();
}

void* FileMap::request_pointer(unsigned int& Bytes)
{
    // .text:06262a30.
    if (View != nullptr && Cursor < Size)
    {
        unsigned int const Remain = Size - Cursor;
        if (Bytes > Remain)
            Bytes = Remain;
        unsigned int const Start = Cursor;
        Cursor = Start + Bytes;
        return View + Start;
    }
    Bytes = 0;
    return nullptr;
}

unsigned int FileMap::read(void* Dest, unsigned int Bytes)
{
    // .text:06262a70: clamp to what is left, advance Cursor, then copy. The copy is skipped
    // only when the source pointer View+Cursor is null; Dest is not checked.
    if (View == nullptr || Cursor >= Size)
        return 0;

    unsigned int const Remain = Size - Cursor;
    if (Bytes > Remain)
        Bytes = Remain;

    char const* const Src = View + Cursor;
    Cursor += Bytes;
    if (Src != nullptr)
        memcpy(Dest, Src, Bytes);
    return Bytes;
}

namespace
{
    // The three read_ptr bodies (.text:0626c010/0626c060/0626c0b0) are one template: an
    // inlined request_pointer for sizeof(T) bytes; on a short read the out pointer is nulled.
    template <typename T>
    bool ReadBiniPtr(FileMap& Map, T*& Out, unsigned int Want)
    {
        unsigned int Got = Want;
        Out = (T*)Map.request_pointer(Got);
        if (Got != Want)
        {
            Out = nullptr;
            return false;
        }
        return true;
    }
}

bool FileMap::read_ptr(BINI_Header*& Out)
{
    // .text:0626c010: 0x0c bytes.
    return ReadBiniPtr(*this, Out, 0x0c);
}

bool FileMap::read_ptr(BINI_Block*& Out)
{
    // .text:0626c060: 4 bytes.
    return ReadBiniPtr(*this, Out, 4);
}

bool FileMap::read_ptr(BINI_Value*& Out)
{
    // .text:0626c0b0: 3 bytes.
    return ReadBiniPtr(*this, Out, 3);
}

void FileMap::handoff_to(FileMap& Other)
{
    // .text:062624c0: Other takes the name and all five handle/view/size/cursor dwords; what
    // Other held before is NOT closed. `this` then forgets its view and handles but keeps its
    // name, Size and Cursor. There is no self-check: handing off to itself drops the handles.
    Other.FileName = FileName;
    Other.FileHandle = FileHandle;
    Other.MappingHandle = MappingHandle;
    Other.View = View;
    Other.Size = Size;
    Other.Cursor = Cursor;

    View = nullptr;
    MappingHandle = nullptr;
    FileHandle = INVALID_HANDLE_VALUE;
}

// ---------------------------------------------------------------------------------------------
// LineParser32 (.text:06262e30..06263010, 0630f170..0630f250)
// ---------------------------------------------------------------------------------------------

LineParser32::LineParser32()
{
    // .text:06262e50: Buffer[0] = 0, Position (+0x404) = 0, Length (+0x400) = 0. The rest of
    // the buffer is not touched.
    Buffer[0] = '\0';
    Position = 0;
    Length = 0;
}

void LineParser32::clear()
{
    // .text:06262e30: the same three stores as the ctor.
    Buffer[0] = '\0';
    Position = 0;
    Length = 0;
}

void LineParser32::set(char const* Text, unsigned int Count)
{
    // .text:06262e70: Count bytes copied unclamped (REP MOVSD / MOVSB), Buffer[Count] = 0,
    // Length = Count, Position = 0. A Count past 0x3ff overruns the buffer as in retail.
    memcpy(Buffer, Text, Count);
    Buffer[Count] = '\0';
    Length = Count;
    Position = 0;
}

bool LineParser32::whitespace(char Ch) const
{
    // .text:06262eb0: ' ' or '\t'.
    return Ch == ' ' || Ch == '\t';
}

void LineParser32::skip_white()
{
    // .text:06262ed0: steps over ' ' / '\t' at Position with no Length bound; the NUL at
    // Buffer[Length] stops it.
    while (Buffer[Position] == ' ' || Buffer[Position] == '\t')
        Position += 1;
}

char LineParser32::get_char() const
{
    // .text:06262ef0: Position < Length (unsigned JNC) -> MOVSX the char, else 0.
    if (Position < Length)
        return Buffer[Position];
    return '\0';
}

bool LineParser32::skip_char(char Ch)
{
    // .text:06262f10.
    if (Position < Length && Buffer[Position] == Ch)
    {
        Position += 1;
        return true;
    }
    return false;
}

int LineParser32::find_char(char Ch) const
{
    // .text:06262f40: the index of the first Ch in [Position, Length), else -1.
    for (unsigned int Index = Position; Index < Length; Index += 1)
    {
        if (Buffer[Index] == Ch)
            return (int)Index;
    }
    return -1;
}

unsigned int LineParser32::read_string(char* Dest, char const* Terminators)
{
    // .text:06262f70: N = strcspn(Buffer + Position, Terminators); N bytes copied to Dest and
    // terminated; Position += N + 1 (over the terminator, or past the NUL when there was
    // none). No Length bound and no null checks.
    char const* const Start = Buffer + Position;
    unsigned int const Count = (unsigned int)strcspn(Start, Terminators);
    memcpy(Dest, Start, Count);
    Dest[Count] = '\0';
    Position += Count + 1u;
    return Count;
}

bool LineParser32::terminate_line(char Ch)
{
    // .text:06262fd0: the first Ch in [Position, Length) becomes the new Length; Position is
    // not touched.
    for (unsigned int Index = Position; Index < Length; Index += 1)
    {
        if (Buffer[Index] == Ch)
        {
            Length = Index;
            return true;
        }
    }
    return false;
}

unsigned int LineParser32::fetch_string(char* Dest, unsigned int EndPos)
{
    // .text:0630f170: Count = EndPos - Position (signed); while Count > 0 trailing ' ' / '\t'
    // of [Position, Position + Count) are dropped; Count bytes copied from Buffer + Position,
    // Dest[Count] = 0. Position is not advanced. A negative Count copies (unsigned)Count bytes
    // as retail's REP MOVSD does.
    int Count = (int)(EndPos - Position);
    if (Count > 0)
    {
        char const* Last = Buffer + Position + Count - 1;
        while (*Last == ' ' || *Last == '\t')
        {
            Count -= 1;
            Last -= 1;
            if (Count <= 0)
                break;
        }
    }
    memcpy(Dest, Buffer + Position, (unsigned int)Count);
    Dest[Count] = '\0';
    return (unsigned int)Count;
}

unsigned int LineParser32::read_terminator(char* Dest, char Term)
{
    // .text:0630f1d0: Count = the run before Term in [Position, Length); Position += Count
    // (left ON the terminator); trailing ' ' / '\t' dropped from the copy only; Count bytes
    // copied and terminated.
    unsigned int const Remain = Length - Position;
    unsigned int Count = 0;
    if (Remain != 0)
    {
        do
        {
            if (Buffer[Position + Count] == Term)
                break;
            Count += 1;
        } while (Count < Remain);
    }
    char const* const Start = Buffer + Position;
    Position += Count;
    while (Count != 0 && (Start[Count - 1u] == ' ' || Start[Count - 1u] == '\t'))
        Count -= 1;
    memcpy(Dest, Start, Count);
    Dest[Count] = '\0';
    return Count;
}

unsigned int LineParser32::read_terminator2(char* Dest, char TermA, char TermB)
{
    // .text:0630f250: like read_terminator with two terminators, but every byte is stored
    // into Dest while scanning and the trailing ' ' / '\t' trim reads Dest.
    unsigned int const Remain = Length - Position;
    unsigned int Count = 0;
    if (Remain != 0)
    {
        do
        {
            char const Ch = Buffer[Position + Count];
            if (Ch == TermA || Ch == TermB)
                break;
            Dest[Count] = Ch;
            Count += 1;
        } while (Count < Remain);
    }
    Position += Count;
    while (Count != 0 && (Dest[Count - 1u] == ' ' || Dest[Count - 1u] == '\t'))
        Count -= 1;
    Dest[Count] = '\0';
    return Count;
}

LineParser32& LineParser32::operator=(LineParser32 const& Other)
{
    // .text:06263010: 0x102 dwords copied (REP MOVSD), no self check.
    memmove(Buffer, Other.Buffer, sizeof(Buffer));
    Length = Other.Length;
    Position = Other.Position;
    return *this;
}

// ---------------------------------------------------------------------------------------------
// TextFile32 (FileMap + line counter)
// ---------------------------------------------------------------------------------------------

TextFile32::TextFile32()
{
    // .text:06263040, the FileMap ctor itself (ICF): LineNum (+0x24) is not written.
}

TextFile32::TextFile32(TextFile32 const& Other)
    : FileMap(Other)
    , LineNum(Other.LineNum)
{
    // .text:06263070: the FileMap copy inlined (string, +0x10..+0x20), then +0x24.
}

TextFile32::~TextFile32()
{
    // .text:06263310, the FileMap dtor itself (ICF).
}

TextFile32& TextFile32::operator=(TextFile32 const& Other)
{
    // .text:062631b0: the FileMap assignment inlined, then +0x24.
    FileMap::operator=(Other);
    LineNum = Other.LineNum;
    return *this;
}

unsigned int TextFile32::get_line_num() const
{
    // .text:06263030: MOV EAX,[this+0x24].
    return LineNum;
}

bool TextFile32::read_line(LineParser32& Line)
{
    // .text:0630f2c0. Start = tell(); LineNum += 1 on every call. A window of up to 0x3ff
    // bytes is taken at Cursor (request_pointer inlined: Cursor += window). The first '\r' or
    // '\n' ends the text; the terminator is consumed, plus the next byte when it is the
    // partner of the pair (byte ^ 7: "\r\n" or "\n\r") and inside the window. The text is
    // copied into the parser, terminated, Length = text length, Position = 0, and Cursor is
    // set back to min(Start + consumed, Size). No line ending in the window: the whole window
    // is the text. Failure (no view, at the end) empties the parser and returns false.
    unsigned int const Start = (View != nullptr) ? Cursor : 0u;
    LineNum += 1;

    if (View != nullptr && Cursor < Size)
    {
        unsigned int Window = 0x3ff;
        unsigned int const Remain = Size - Cursor;
        if (Remain < Window)
            Window = Remain;
        char const* const Src = View + Cursor;
        Cursor += Window;
        if (Src != nullptr)
        {
            unsigned int TextLength = Window;
            unsigned int Consumed = Window;
            for (unsigned int Index = 0; Index < Window; Index += 1)
            {
                char const Ch = Src[Index];
                if (Ch == '\r' || Ch == '\n')
                {
                    TextLength = Index;
                    unsigned int Next = Index + 1u;
                    if (Next < Window && Src[Next] == (char)(Ch ^ 7))
                        Next += 1u;
                    Consumed = Next;
                    break;
                }
            }

            memcpy(Line.Buffer, Src, TextLength);
            Line.Buffer[TextLength] = '\0';
            Line.Length = TextLength;
            Line.Position = 0;

            unsigned int const NewCursor = Start + Consumed;
            if (View != nullptr)
                Cursor = (NewCursor <= Size) ? NewCursor : Size;
            return true;
        }
    }

    Line.Buffer[0] = '\0';
    Line.Position = 0;
    Line.Length = 0;
    return false;
}
