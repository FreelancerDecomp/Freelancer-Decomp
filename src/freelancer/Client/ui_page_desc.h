// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// PAGE_*.txt layout descriptor — the object retail embeds in every PAGE screen
// (PageServerSelect+0x8c, PageCharacterSelect+0x28c, ...) and loads with FUN_00592230.
//
// Retail layout (FUN_00592230 / FUN_00594540 / FUN_00594580 / FUN_00592080):
//   +0x00  std::list<FUiWidgetEntry*>  entries in file order (FUN_005947a0 push_back,
//                                      size at +0x08)
//   +0x0c  FUiPageData { float Aspect; FUiWidgetEntry** Entries; } — the part FUN_00594580
//          hands to UIPage::MasterWin (ctor FUN_00591ae0 stores it at MasterWin+0x84)
//   +0x14  std::set<void*> of every operator-new block the loader made (FUN_0046d170 insert);
//          FUN_00594540 deletes and erases them on reload, FUN_00592080 on destruction.
//
// No retail ABI: the port keeps the same three members with std containers.

#include <list>
#include <set>

// Written to FUiPageData::Aspect on a successful load (float bits 0x3faaa993 = 1.33333).
// Instantiation (FUN_00594590) copies it into UIPage::Page+0x18.
static unsigned const kUiPageLoadMagicBits = 0x3faaa993u;

// One widget line — retail operator_new(0x20), FUN_00592230.
struct FUiWidgetEntry
{
    char* TypeName;          // +0x00  "UIStaticText", ... (WINFACTORY class key)
    void* Payload;           // +0x04  type-specific blob (FUiPayload*)
    char* InstanceName;      // +0x08  nickname; key of the UIPage::Page name map
    float Left;              // +0x0C  1st float — translation X at FUN_005916b0
    float Top;               // +0x10  2nd float — translation Y at FUN_005916b0
    float Right;             // +0x14  3rd float — also payload +0x00
    float Bottom;            // +0x18  4th float — also payload +0x04
    // +0x1C — child entry array (null-terminated) walked by FUN_005916b0's recursion.
    // FUN_00592230 always writes 0 here.
    FUiWidgetEntry** NestedChildren;
};

static_assert(sizeof(void*) != 4 || sizeof(FUiWidgetEntry) == 0x20, "FUiWidgetEntry is 0x20");

// UIStaticText / UITextEdit payload — operator_new(0x0C)
struct FUiPayloadText
{
    float Right;             // +0x00 3rd float
    float Bottom;            // +0x04 4th float
    wchar_t* CaptionUtf16;   // +0x08 optional "%[^"]" caption, each byte MOVSX-widened
};

// UITextMsgButton payload — operator_new(0x18)
struct FUiPayloadTextMsgButton
{
    float Right;             // +0x00
    float Bottom;            // +0x04
    wchar_t* CaptionUtf16;   // +0x08 MOVSX-widened caption
    int ExtraInt;            // +0x0C ftol of the float after the caption
    int ExtraId;             // +0x10 trailing %d (0 when absent)
    char* StyleName;         // +0x14 "<none>" → nullptr; "" / "<default>" → kUiPageDefaultStyle;
                             //       anything else → a copy
};

// UIListBox / UIWinDeform / UIRichTextWin / UIShipViewControl — operator_new(0x08)
struct FUiPayloadBounds
{
    float Right;             // +0x00
    float Bottom;            // +0x04
};

// UIWinObject payload — operator_new(0x10)
struct FUiPayloadWinObject
{
    float Right;             // +0x00
    float Bottom;            // +0x04
    char* ExtraName;         // +0x08 trailing %s (mesh path)
    float ExtraFloat;        // +0x0C 5th float
};

// UITable / UIFastTable payload — operator_new(0x10)
struct FUiPayloadTable
{
    float Right;             // +0x00
    float Bottom;            // +0x04
    int Int5;                // +0x08 ftol(5th float) — UITable 00593c5e, UIFastTable 00593edc
    int Int6;                // +0x0C ftol(6th float) — UITable 00593c52, UIFastTable 00593ed0
};

// UIScroll / UICycle payload — operator_new(0x1C)
struct FUiPayloadScrollCycle
{
    float Right;             // +0x00
    float Bottom;            // +0x04
    float Extra0;            // +0x08 5th float
    float Extra1;            // +0x0C 6th float
    float Extra2;            // +0x10 7th float
    float Extra3;            // +0x14 8th float
    unsigned char Flag;      // +0x18 trailing %d != 0 (0 when absent)
    unsigned char Pad[3];
};

// What FUN_00594580 returns (&desc+0x0c) and UIPage::MasterWin keeps at +0x84.
struct FUiPageData
{
    float Aspect;                   // +0x00 kUiPageLoadMagicBits after a successful load, else 0
    FUiWidgetEntry** Entries;       // +0x04 null-terminated copy of the list, or null
};

// Retail .data s_<default>_006155f0 — the shared StyleName for "" and "<default>".
extern char kUiPageDefaultStyle[];

class FUiPageDesc
{
public:
    FUiPageDesc();
    // Retail teardown FUN_00592080 (set part) + list/set destructors: every tracked block is
    // deleted, then the containers go.
    ~FUiPageDesc();

    // FUN_00592230 — returns the retail success byte (cStack_8e9).
    bool LoadFromFile(char const* FullPath);

    // FUN_00594580 — LEA EAX,[ECX+0xc].
    FUiPageData* GetPageData() { return &Data; }

    unsigned GetEntryCount() const { return static_cast<unsigned>(EntryList.size()); }

private:
    // FUN_00594540 — delete every tracked block and erase it from the set.
    void ClearAllocations();
    // operator_new + FUN_0046d170 insert into Allocations.
    void* TrackedNew(unsigned Size);
    char* TrackedDup(char const* Src);
    wchar_t* TrackedWiden(char const* Src);
    FUiWidgetEntry* NewEntry(char const* Type, char const* Name, float Left, float Top,
                             float Right, float Bottom);
    // The repeated per-type branches of FUN_00592230 (same code in retail for each listed
    // type); null on a field-count miss.
    FUiWidgetEntry* ParseBoundsLine(char const* Line, char* Type, char* Name);
    FUiWidgetEntry* ParseTableLine(char const* Line, char* Type, char* Name);
    FUiWidgetEntry* ParseScrollCycleLine(char const* Line, char* Type, char* Name);

    std::list<FUiWidgetEntry*> EntryList;  // retail +0x00
    FUiPageData Data;                      // retail +0x0c
    std::set<void*> Allocations;           // retail +0x14

    FUiPageDesc(FUiPageDesc const&) = delete;
    FUiPageDesc& operator=(FUiPageDesc const&) = delete;
};
