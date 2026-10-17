// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The news vendor of a base (retail NN_News, freelancer.exe 0x004a3f90..0x004a60b6,
// "E:\FL\Scratch\Source\Client\nn\NN_NewsVendor.cpp").
//
// RTTI .?AVNN_News@@ : WinControl, Event::Handler. operator_new(0x674) (0x004a6038).
// WINFACTORY<NN_News> DAT_006720cc "NN_News": static init LAB_004a3fc0 (ctor FUN_004a5fb0, atexit
// LAB_004a3fe0 -> FUN_00579380); Create FUN_004a6020 (operator_new, the ctor FUN_004a3ff0, the
// class-name pointer at +0x10, slot 9; a failed Create is followed by the scalar deleting
// destructor). FUN_004a5fb0 is the WINFACTORY ctor inlined for this type (name clamped to 31,
// FUN_0059dab0, vftable 0x005d4414).
//
// vftable 0x005d3fdc -- WinControl's with these overrides (raw dword dump):
//   0 FUN_004a40f0 scalar deleting dtor (FUN_004a4110), 9 LAB_004a4250 Create, 11 LAB_004a5e50
//   Update, 13 FUN_004a5d80 Draw, 42 LAB_004a4d60 SetProperty.
// IDataViewOwner face +0x7c: vftable 0x005d3fd0 (WinControl's).
// Event::Handler face +0x32c: vftable 0x005d3fc8, slot 0 LAB_004a5f40.
//
// Five rows of the base's news (the LocationController's NewsBroadcastProperties bucket, copied
// and sorted once by LoadNews), a scroll bar, the selected item's headline and story, its icon, a
// video window whose material "MV_vidmaterial" gets the item's logo texture swapped in, and a
// ticker scrolling the flagged headlines and the base's commodity prices.
//
// Layout: retail's -- no member is a std container, so every offset is retail's.

#include "event_handler.h"
#include "swapped_material.h"
#include "wincontrol_base.h"

#include "common_id.h" // ID_String

#include <stddef.h>
#include <stdint.h>

class NewsBroadcastProperties;
class RichTextWin;
class UiScroll;

class NnNews : public WinControl, public EventHandler
{
public:
    // cdecl (Context): called when the window is closed by its button or USER_CANCEL. No writer
    // of +0x33c / +0x340 exists in freelancer.exe besides the ctor's zero.
    typedef void (*FResultCallback)(void* Context);

    // FUN_004a3ff0.
    NnNews();
    // FUN_004a4110 (FUN_004a40f0 the scalar deleting wrapper).
    ~NnNews() override;

    // slot 9 LAB_004a4250 (RET 8) -- every control, the news, the ticker, the open sound,
    // Win3D's Create last.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_004a5e50 -- the ticker scrolled by the frame time, then Win3D's Update.
    void Update() override;
    // slot 13 FUN_004a5d80 -- row layers and the box column, WinControl's Draw, then the button
    // montage flushed.
    void Draw() override;
    // slot 42 LAB_004a4d60 (RET 0xc) -- clicks (0x26), hover (0x2c / 0x2d), scroll (0x3e, which
    // falls into the hover walk), move (0x42), close (0x4e); returns 0.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face LAB_004a5f40 (RET 4) -- USER_CANCEL (0x10) closes the window.
    bool OnEvent(unsigned MsgId) override;

    float BasePosition[3];               // +0x330 {0, -0.1, -1.498}, set by Create
    FResultCallback ResultCallback;      // +0x33c
    void* ResultContext;                 // +0x340
    wchar_t* TickerText;                 // +0x344 _wcsdup (CRT free)
    int* TickerWidths;                   // +0x348 operator_new(len * 4), a width per character
    int TickerLength;                    // +0x34c
    float TickerScroll;                  // +0x350 pixels into TickerText[TickerIndex]
    int TickerIndex;                     // +0x354 first character shown
    wchar_t TickerView[0x102];           // +0x358 the visible slice (0x100 chars + NUL)
    int MaterialGroup;                   // +0x55c MV_videoscreen.3db (FUN_0041c850); the ctor
                                         //        leaves it unwritten
    char* VideoTexture;                  // +0x560 malloc_strcpy of the swapped-in texture name
    FSwappedMaterial VideoMaterial;      // +0x564 "MV_vidmaterial"
    int ScrollOffset;                    // +0x578 first item in row 0
    int Field_0x57c;                     // +0x57c TODO: zeroed by the ctor, LoadNews, Create and
                                         //        SetProperty 0x26, never read
    int Selected;                        // +0x580 item index, -1 none
    NewsBroadcastProperties* News;       // +0x584 new[] sorted copy of the bucket
    int NewsCount;                       // +0x588
    char const** IconPaths;              // +0x58c 7 mesh paths (operator_new(0x1c))
    unsigned IconPathCount;              // +0x590 7
    int IconIds[7];                      // +0x594 the icons' tooltip ids
    unsigned Field_0x5b0;                // +0x5b0 TODO: not written or read by this class
    WinControl* CloseButton;             // +0x5b4 "NewsCloseButton"
    WinControl* Ticker;                  // +0x5b8 "NewsTicker"
    WinControl* HeadlineIcons[5];        // +0x5bc "NewsHeadlineIcon%d"
    WinControl* IconFrames[5];           // +0x5d0 "NewsHeadlineIconFrame%d"
    WinControl* HeadlineBoxes[5];        // +0x5e4 "NewsHeadlineBox%d"
    WinControl* Headlines[5];            // +0x5f8 "NewsHeadlineHeadline%d"
    float HeadlinePositions[5][3];       // +0x60c each headline's position, from Create
    UiScroll* Scroll;                    // +0x648 "NewsHeadlineScroll"
    WinControl* StoryHeadline;           // +0x64c "NewsStoryHeadline"
    RichTextWin* StoryText;              // +0x650 "NewsStoryText"
    unsigned Field_0x654;                // +0x654 TODO: not written or read by this class
    WinControl* StoryIcon;               // +0x658 "NewsStoryIcon"
    unsigned Field_0x65c;                // +0x65c TODO: not written or read by this class
    int Hovered;                         // +0x660 item index under the mouse, -1 none
    int Clicked;                         // +0x664 row last clicked, -1 none
    ID_String OpenSound;                 // +0x668 "news_vendor_open"
    unsigned char bOpenSoundPlaying;     // +0x66c
    unsigned char Pad66d[3];
    WinControl* Background;              // +0x670 "NewsBackground"

private:
    // The tail every close shares (e.g. 0x004a4d90..0x004a4db6): FUN_0043d2a0 on every child
    // (the next sibling read first), slot 10, then slot 0 with 1.
    void DeleteSelf();
    // FUN_004a4fd0 -- copy the current base's news: flag +9 first, then category 1, then flag
    // +8, then the rest; rows from the top, item 0 selected.
    void LoadNews();
    // FUN_004a5270 -- fill the five rows from ScrollOffset.
    void RefreshRows();
    // FUN_004a5580 -- the selected item's headline, story, logo texture and icon.
    void ShowSelected();
    // FUN_004a5890 -- the ticker text (flagged headlines, then commodity prices, repeated until
    // at least 0x80 characters), its character widths, then TickerShow(0, 0).
    void BuildTicker();
    // FUN_004a5ca0 (RET 8) -- the slice of the ticker that fits from Start, shifted by Offset.
    void TickerShow(int Start, float Offset);
    // The two FUN_00459950 calls on the selected item (Create, SetProperty 0x26).
    void RevealSelected();
    // Draw's `[this + Row*4 + 0x5d0]` / `+0x5e4` with Row outside 0..4 (a selection or hover
    // scrolled out of the rows): the dword that far from the array in retail's layout.
    uintptr_t RowDword(size_t ArrayOffset, int Index) const;

    NnNews(NnNews const&) = delete;
    NnNews& operator=(NnNews const&) = delete;
};

static_assert(offsetof(NnNews, BasePosition) == 0x330, "NN_News +0x330");
static_assert(offsetof(NnNews, ResultCallback) == 0x33c, "NN_News +0x33c");
static_assert(offsetof(NnNews, TickerText) == 0x344, "NN_News +0x344");
static_assert(offsetof(NnNews, TickerView) == 0x358, "NN_News +0x358");
static_assert(offsetof(NnNews, MaterialGroup) == 0x55c, "NN_News +0x55c");
static_assert(offsetof(NnNews, VideoMaterial) == 0x564, "NN_News +0x564");
static_assert(offsetof(NnNews, ScrollOffset) == 0x578, "NN_News +0x578");
static_assert(offsetof(NnNews, News) == 0x584, "NN_News +0x584");
static_assert(offsetof(NnNews, IconIds) == 0x594, "NN_News +0x594");
static_assert(offsetof(NnNews, CloseButton) == 0x5b4, "NN_News +0x5b4");
static_assert(offsetof(NnNews, HeadlineIcons) == 0x5bc, "NN_News +0x5bc");
static_assert(offsetof(NnNews, IconFrames) == 0x5d0, "NN_News +0x5d0");
static_assert(offsetof(NnNews, HeadlineBoxes) == 0x5e4, "NN_News +0x5e4");
static_assert(offsetof(NnNews, Headlines) == 0x5f8, "NN_News +0x5f8");
static_assert(offsetof(NnNews, HeadlinePositions) == 0x60c, "NN_News +0x60c");
static_assert(offsetof(NnNews, Scroll) == 0x648, "NN_News +0x648");
static_assert(offsetof(NnNews, Hovered) == 0x660, "NN_News +0x660");
static_assert(offsetof(NnNews, OpenSound) == 0x668, "NN_News +0x668");
static_assert(offsetof(NnNews, Background) == 0x670, "NN_News +0x670");
static_assert(sizeof(NnNews) == 0x674, "retail NN_News is 0x674 bytes (operator_new 0x004a6038)");
