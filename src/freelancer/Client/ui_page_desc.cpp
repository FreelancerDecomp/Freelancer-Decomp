// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_page_desc.h"

#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include <new>
#include <string>

// Retail .data s_<default>_006155f0.
char kUiPageDefaultStyle[] = "<default>";

namespace
{
    // FUN_00592230 matches each type with REPE CMPSB over strlen+1 bytes (the literal's NUL is
    // part of the compare), i.e. an exact match. sizeof(Literal) is that byte count.
    template <unsigned N>
    bool TypeIs(char const* Type, char const (&Literal)[N])
    {
        return memcmp(Type, Literal, N) == 0;
    }

    // MSVCRT ftol — truncation toward zero.
    int Ftol(float Value)
    {
        return static_cast<int>(Value);
    }
}

FUiPageDesc::FUiPageDesc()
{
    Data.Aspect = 0.0f;
    Data.Entries = nullptr;
}

FUiPageDesc::~FUiPageDesc()
{
    // FUN_00592080: delete every block of the +0x14 set, then the containers' own storage.
    ClearAllocations();
}

void FUiPageDesc::ClearAllocations()
{
    // FUN_00594540: for (it = begin; it != end; ) { operator delete(*it); it = erase(it); }
    std::set<void*>::iterator It = Allocations.begin();
    while (It != Allocations.end())
    {
        ::operator delete(*It);
        It = Allocations.erase(It);
    }
}

void* FUiPageDesc::TrackedNew(unsigned Size)
{
    // operator_new(Size) followed by FUN_0046d170(this+0x14) set insert of the result.
    void* Block = ::operator new(Size);
    Allocations.insert(Block);
    return Block;
}

char* FUiPageDesc::TrackedDup(char const* Src)
{
    // REPNE SCASB length + 1, operator_new, byte loop copy, NUL (e.g. 00592480..005924c6).
    unsigned Length = static_cast<unsigned>(strlen(Src));
    char* Copy = static_cast<char*>(TrackedNew(Length + 1));
    unsigned Index = 0;
    while (Src[Index] != '\0')
    {
        Copy[Index] = Src[Index];
        ++Index;
    }
    Copy[Index] = '\0';
    return Copy;
}

wchar_t* FUiPageDesc::TrackedWiden(char const* Src)
{
    // 005925ed..00592634: operator_new(len*2+2); each byte MOVSX DX,DL (sign-extended) into a
    // 16-bit unit; terminating 0 unit.
    unsigned Length = static_cast<unsigned>(strlen(Src));
    wchar_t* Wide = static_cast<wchar_t*>(TrackedNew(Length * 2 + 2));
    unsigned Index = 0;
    while (Src[Index] != '\0')
    {
        Wide[Index] = static_cast<wchar_t>(static_cast<short>(static_cast<signed char>(Src[Index])));
        ++Index;
    }
    Wide[Index] = 0;
    return Wide;
}

FUiWidgetEntry* FUiPageDesc::NewEntry(char const* Type, char const* Name, float Left, float Top,
                                      float Right, float Bottom)
{
    // Common head of every branch (e.g. 00592404..00592565): operator_new(0x20), the four
    // floats, +0x1c = 0, type copy at +0x00, nickname copy at +0x08.
    FUiWidgetEntry* Entry = static_cast<FUiWidgetEntry*>(TrackedNew(sizeof(FUiWidgetEntry)));
    Entry->Bottom = Bottom;
    Entry->Right = Right;
    Entry->Left = Left;
    Entry->Top = Top;
    Entry->NestedChildren = nullptr;
    Entry->TypeName = TrackedDup(Type);
    Entry->InstanceName = TrackedDup(Name);
    Entry->Payload = nullptr;
    return Entry;
}

FUiWidgetEntry* FUiPageDesc::ParseBoundsLine(char const* Line, char* Type, char* Name)
{
    // UIListBox / UIWinDeform / UIRichTextWin / UIShipViewControl: "%s %s %f %f %f %f" == 6,
    // 0x08 payload {F3, F4}.
    float F1 = 0.0f;
    float F2 = 0.0f;
    float F3 = 0.0f;
    float F4 = 0.0f;
    int Count = sscanf(Line, "%s %s %f %f %f %f", Type, Name, &F1, &F2, &F3, &F4);
    if (Count != 6)
        return nullptr;
    FUiWidgetEntry* Entry = NewEntry(Type, Name, F1, F2, F3, F4);
    FUiPayloadBounds* Payload = static_cast<FUiPayloadBounds*>(TrackedNew(sizeof(FUiPayloadBounds)));
    Entry->Payload = Payload;
    Payload->Right = F3;
    Payload->Bottom = F4;
    return Entry;
}

FUiWidgetEntry* FUiPageDesc::ParseTableLine(char const* Line, char* Type, char* Name)
{
    // UITable / UIFastTable: "%s %s %f %f %f %f %f %f" == 8; +0x0c = ftol(F6) is written before
    // +0x08 = ftol(F5) (00593c52 / 00593c5e).
    float F1 = 0.0f;
    float F2 = 0.0f;
    float F3 = 0.0f;
    float F4 = 0.0f;
    float F5 = 0.0f;
    float F6 = 0.0f;
    int Count = sscanf(Line, "%s %s %f %f %f %f %f %f", Type, Name, &F1, &F2, &F3, &F4, &F5, &F6);
    if (Count != 8)
        return nullptr;
    FUiWidgetEntry* Entry = NewEntry(Type, Name, F1, F2, F3, F4);
    FUiPayloadTable* Payload = static_cast<FUiPayloadTable*>(TrackedNew(sizeof(FUiPayloadTable)));
    Entry->Payload = Payload;
    Payload->Right = F3;
    Payload->Bottom = F4;
    Payload->Int6 = Ftol(F6);
    Payload->Int5 = Ftol(F5);
    return Entry;
}

FUiWidgetEntry* FUiPageDesc::ParseScrollCycleLine(char const* Line, char* Type, char* Name)
{
    // UIScroll / UICycle: 10 or 11 fields; the trailing %d slot is zeroed before the scan
    // (00593f66 / 00594208).
    float F1 = 0.0f;
    float F2 = 0.0f;
    float F3 = 0.0f;
    float F4 = 0.0f;
    float F5 = 0.0f;
    float F6 = 0.0f;
    float F7 = 0.0f;
    float F8 = 0.0f;
    int FlagInt = 0;
    int Count = sscanf(Line, "%s %s %f %f %f %f %f %f %f %f %d", Type, Name, &F1, &F2, &F3, &F4,
                       &F5, &F6, &F7, &F8, &FlagInt);
    if (Count != 10 && Count != 11)
        return nullptr;
    FUiWidgetEntry* Entry = NewEntry(Type, Name, F1, F2, F3, F4);
    FUiPayloadScrollCycle* Payload =
        static_cast<FUiPayloadScrollCycle*>(TrackedNew(sizeof(FUiPayloadScrollCycle)));
    Entry->Payload = Payload;
    Payload->Right = F3;
    Payload->Bottom = F4;
    Payload->Extra0 = F5;
    Payload->Extra1 = F6;
    Payload->Extra2 = F7;
    Payload->Extra3 = F8;
    Payload->Flag = FlagInt != 0 ? 1 : 0;
    return Entry;
}

bool FUiPageDesc::LoadFromFile(char const* FullPath)
{
    // 0059224f: setlocale(LC_NUMERIC, "C") and keep its RETURN VALUE (the new locale name) in a
    // std::string; the same string is handed back to setlocale at 005944e8, so the net effect
    // leaves LC_NUMERIC at "C".
    std::string SavedLocale(setlocale(LC_NUMERIC, "C"));
    bool bOk = false;

    // 005922cd FUN_00594540(this+0x14); 005922ec FUN_004a2f80 erase(begin, end) of the list;
    // 005922f8 +0x0c = +0x10 = 0.
    ClearAllocations();
    EntryList.clear();
    Data.Aspect = 0.0f;
    Data.Entries = nullptr;

    FILE* File = fopen(FullPath, "rt");
    if (File != nullptr)
    {
        bOk = true;

        char Line[0x100];
        char Type[0x100];
        char Name[0x100];
        char Caption[0x100];
        char Style[0x100];
        float F1 = 0.0f;
        float F2 = 0.0f;
        float F3 = 0.0f;
        float F4 = 0.0f;
        float F5 = 0.0f;
        // UITextMsgButton's float after the caption. Retail does not reset this stack slot per
        // line, so a 7-field line reuses the previous line's value (first use: stale stack).
        float MsgExtraFloat = 0.0f;

        while (fgets(Line, 0x100, File) != nullptr)
        {
            // 00592341..00592386: skip '#', leading whitespace (isspace on MOVSX char) and
            // lines without a first token.
            if (Line[0] == '#')
                continue;
            if (isspace(static_cast<int>(static_cast<signed char>(Line[0]))) != 0)
                continue;
            if (sscanf(Line, "%s", Type) != 1)
                continue;

            // 005923a7: the caption buffer is cleared for every line that reaches the chain.
            Caption[0] = '\0';
            FUiWidgetEntry* Entry = nullptr;

            if (TypeIs(Type, "UIStaticText"))
            {
                int Count = sscanf(Line, "%s %s %f %f %f %f \"%[^\"]", Type, Name, &F1, &F2, &F3,
                                   &F4, Caption);
                if (Count == 6 || Count == 7)
                {
                    Entry = NewEntry(Type, Name, F1, F2, F3, F4);
                    FUiPayloadText* Payload =
                        static_cast<FUiPayloadText*>(TrackedNew(sizeof(FUiPayloadText)));
                    Entry->Payload = Payload;
                    Payload->Right = F3;
                    Payload->Bottom = F4;
                    Payload->CaptionUtf16 = TrackedWiden(Caption);
                }
            }
            else if (TypeIs(Type, "UITextMsgButton"))
            {
                // 005926a8: style[0] = 0 and the trailing %d slot = 0 before the scan.
                int ExtraId = 0;
                Style[0] = '\0';
                int Count = sscanf(Line, "%s %s %f %f %f %f \"%[^\"]\" %f \"%[^\"]\" %d", Type,
                                   Name, &F1, &F2, &F3, &F4, Caption, &MsgExtraFloat, Style,
                                   &ExtraId);
                if (Count == 7 || Count == 8 || Count == 9 || Count == 10)
                {
                    Entry = NewEntry(Type, Name, F1, F2, F3, F4);
                    FUiPayloadTextMsgButton* Payload = static_cast<FUiPayloadTextMsgButton*>(
                        TrackedNew(sizeof(FUiPayloadTextMsgButton)));
                    Entry->Payload = Payload;
                    Payload->Right = F3;
                    Payload->Bottom = F4;
                    Payload->CaptionUtf16 = TrackedWiden(Caption);
                    Payload->ExtraInt = Ftol(MsgExtraFloat);
                    Payload->ExtraId = ExtraId;
                    // 00592994: 7-byte compare with "<none>" → null; empty or equal to
                    // "<default>" → the shared .data string; otherwise a tracked copy.
                    if (TypeIs(Style, "<none>"))
                        Payload->StyleName = nullptr;
                    else if (Style[0] == '\0' || strcmp(Style, kUiPageDefaultStyle) == 0)
                        Payload->StyleName = kUiPageDefaultStyle;
                    else
                        Payload->StyleName = TrackedDup(Style);
                }
            }
            else if (TypeIs(Type, "UITextEdit"))
            {
                int Count = sscanf(Line, "%s %s %f %f %f %f \"%[^\"]", Type, Name, &F1, &F2, &F3,
                                   &F4, Caption);
                if (Count == 6 || Count == 7)
                {
                    Entry = NewEntry(Type, Name, F1, F2, F3, F4);
                    FUiPayloadText* Payload =
                        static_cast<FUiPayloadText*>(TrackedNew(sizeof(FUiPayloadText)));
                    Entry->Payload = Payload;
                    Payload->Right = F3;
                    Payload->Bottom = F4;
                    Payload->CaptionUtf16 = TrackedWiden(Caption);
                }
            }
            else if (TypeIs(Type, "UIListBox"))
            {
                Entry = ParseBoundsLine(Line, Type, Name);         // 00592db3
            }
            else if (TypeIs(Type, "UIWinObject"))
            {
                // 00593014: the trailing %s lands in the caption buffer.
                int Count = sscanf(Line, "%s %s %f %f %f %f %f %s", Type, Name, &F1, &F2, &F3,
                                   &F4, &F5, Caption);
                if (Count == 8)
                {
                    Entry = NewEntry(Type, Name, F1, F2, F3, F4);
                    FUiPayloadWinObject* Payload = static_cast<FUiPayloadWinObject*>(
                        TrackedNew(sizeof(FUiPayloadWinObject)));
                    Entry->Payload = Payload;
                    Payload->Right = F3;
                    Payload->Bottom = F4;
                    Payload->ExtraName = TrackedDup(Caption);
                    Payload->ExtraFloat = F5;
                }
            }
            else if (TypeIs(Type, "UIWinDeform"))
            {
                Entry = ParseBoundsLine(Line, Type, Name);         // 0059330a
            }
            else if (TypeIs(Type, "UIRichTextWin"))
            {
                Entry = ParseBoundsLine(Line, Type, Name);         // 00593562
            }
            else if (TypeIs(Type, "UIShipViewControl"))
            {
                Entry = ParseBoundsLine(Line, Type, Name);         // 005937c1
            }
            else if (TypeIs(Type, "UITable"))
            {
                Entry = ParseTableLine(Line, Type, Name);          // 00593a24
            }
            else if (TypeIs(Type, "UIFastTable"))
            {
                Entry = ParseTableLine(Line, Type, Name);          // 00593c9e
            }
            else if (TypeIs(Type, "UIScroll"))
            {
                Entry = ParseScrollCycleLine(Line, Type, Name);    // 00593f1c
            }
            else if (TypeIs(Type, "UICycle"))
            {
                Entry = ParseScrollCycleLine(Line, Type, Name);    // 005941be
            }

            if (Entry == nullptr)
            {
                // 00594459: unknown type or a field-count miss clears the success byte and
                // leaves the loop. Entries already pushed stay in the list; +0x0c/+0x10 stay 0.
                bOk = false;
                break;
            }

            // LAB_0059442e: FUN_005947a0 — push_back on the +0x00 list.
            EntryList.push_back(Entry);
        }

        fclose(File);

        if (bOk)
        {
            // 00594474..005944d1: aspect magic, a tracked (count+1)*4 array, list copied in
            // order, null terminator.
            unsigned MagicBits = kUiPageLoadMagicBits;
            memcpy(&Data.Aspect, &MagicBits, sizeof(float));
            unsigned Count = static_cast<unsigned>(EntryList.size());
            FUiWidgetEntry** Array =
                static_cast<FUiWidgetEntry**>(TrackedNew(Count * 4 + 4));
            Data.Entries = Array;
            unsigned Index = 0;
            for (std::list<FUiWidgetEntry*>::iterator It = EntryList.begin(); It != EntryList.end();
                 ++It)
            {
                Array[Index] = *It;
                ++Index;
            }
            Array[Index] = nullptr;
        }
    }

    // 005944db: restore from the kept string.
    setlocale(LC_NUMERIC, SavedLocale.c_str());
    return bOk;
}
